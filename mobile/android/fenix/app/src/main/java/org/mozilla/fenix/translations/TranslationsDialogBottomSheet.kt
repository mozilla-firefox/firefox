/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.translations

import android.content.res.Configuration
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.Dropdown
import mozilla.components.compose.base.InfoCard
import mozilla.components.compose.base.InfoType
import mozilla.components.compose.base.LinkText
import mozilla.components.compose.base.LinkTextState
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.button.TextButton
import mozilla.components.compose.base.menu.MenuItem.CheckableItem
import mozilla.components.compose.base.text.Text
import mozilla.components.concept.engine.translate.Language
import mozilla.components.concept.engine.translate.TranslationError
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.PreviewThemeProvider
import org.mozilla.fenix.theme.Theme
import java.util.Locale
import mozilla.components.ui.icons.R as iconsR

/**
 * Firefox Translations bottom sheet dialog.
 *
 * @param translationsDialogState The current state of the Translations bottom sheet dialog.
 * @param learnMoreUrl The learn more link for translations website.
 * @param showPageSettings Whether the entry point to page settings should be shown or not.
 * @param showFirstTimeFlow Whether translations first flow should be shown.
 * @param onSettingClicked Invoked when the user clicks on the settings button.
 * @param onLearnMoreClicked Invoked when the user clicks on the "Learn More" button.
 * @param onPositiveButtonClicked Invoked when the user clicks on the positive button.
 * @param onNegativeButtonClicked Invoked when the user clicks on the negative button.
 * @param onFromDropdownSelected Invoked when the user selects an item on the from dropdown.
 * @param onToDropdownSelected Invoked when the user selects an item on the to dropdown.
 */
@Suppress("LongParameterList")
@Composable
fun TranslationsDialogBottomSheet(
    translationsDialogState: TranslationsDialogState,
    learnMoreUrl: String,
    showPageSettings: Boolean,
    showFirstTimeFlow: Boolean = false,
    onSettingClicked: () -> Unit,
    onLearnMoreClicked: () -> Unit,
    onPositiveButtonClicked: () -> Unit,
    onNegativeButtonClicked: () -> Unit,
    onFromDropdownSelected: (Language) -> Unit,
    onToDropdownSelected: (Language) -> Unit,
) {
    Column(modifier = Modifier.padding(16.dp)) {
        TranslationsDialogHeader(
            title = if (translationsDialogState.isTranslated && translationsDialogState.translatedPageTitle != null) {
                translationsDialogState.translatedPageTitle
            } else {
                getTranslationsDialogTitle(
                    showFirstTime = showFirstTimeFlow,
                )
            },
            showPageSettings = showPageSettings,
            onSettingClicked = onSettingClicked,
        )

        if (showFirstTimeFlow) {
            Spacer(modifier = Modifier.height(8.dp))

            TranslationsDialogInfoMessage(
                learnMoreUrl = learnMoreUrl,
                onLearnMoreClicked = onLearnMoreClicked,
            )
        }

        DialogContentBaseOnTranslationState(
            translationsDialogState = translationsDialogState,
            onPositiveButtonClicked = onPositiveButtonClicked,
            onNegativeButtonClicked = onNegativeButtonClicked,
            onFromDropdownSelected = onFromDropdownSelected,
            onToDropdownSelected = onToDropdownSelected,
        )
    }
}

/**
 * Dialog content will adapt based on the [TranslationsDialogState].
 *
 * @param translationsDialogState The current state of the Translations bottom sheet dialog.
 * @param onPositiveButtonClicked Invoked when the user clicks on the positive button.
 * @param onNegativeButtonClicked Invoked when the user clicks on the negative button.
 * @param onFromDropdownSelected Invoked when the user selects an item on the from dropdown.
 * @param onToDropdownSelected Invoked when the user selects an item on the to dropdown.
 */
@Suppress("LongParameterList")
@Composable
private fun DialogContentBaseOnTranslationState(
    translationsDialogState: TranslationsDialogState,
    onPositiveButtonClicked: () -> Unit,
    onNegativeButtonClicked: () -> Unit,
    onFromDropdownSelected: (Language) -> Unit,
    onToDropdownSelected: (Language) -> Unit,
) {
    if (translationsDialogState.error != null) {
        DialogContentAnErrorOccurred(
            translationsDialogState = translationsDialogState,
            onFromDropdownSelected = onFromDropdownSelected,
            onToDropdownSelected = onToDropdownSelected,
            onPositiveButtonClicked = onPositiveButtonClicked,
            onNegativeButtonClicked = onNegativeButtonClicked,
        )
    } else if (translationsDialogState.isTranslated) {
        DialogContentTranslated(
            translateToLanguages = translationsDialogState.toLanguages,
            onFromDropdownSelected = onFromDropdownSelected,
            onToDropdownSelected = onToDropdownSelected,
            onPositiveButtonClicked = onPositiveButtonClicked,
            onNegativeButtonClicked = onNegativeButtonClicked,
            positiveButtonType = translationsDialogState.positiveButtonType,
            initialTo = translationsDialogState.initialTo,
        )
    } else {
        Spacer(modifier = Modifier.height(16.dp))

        TranslationsDialogContent(
            translateFromLanguages = translationsDialogState.fromLanguages,
            translateToLanguages = translationsDialogState.toLanguages,
            initialFrom = translationsDialogState.initialFrom,
            initialTo = translationsDialogState.initialTo,
            onFromDropdownSelected = onFromDropdownSelected,
            onToDropdownSelected = onToDropdownSelected,
        )

        Spacer(modifier = Modifier.height(16.dp))

        TranslationsDialogActionButtons(
            positiveButtonText = stringResource(id = R.string.translations_bottom_sheet_positive_button),
            negativeButtonText = stringResource(id = R.string.translations_bottom_sheet_negative_button),
            positiveButtonType = translationsDialogState.positiveButtonType,
            onNegativeButtonClicked = onNegativeButtonClicked,
            onPositiveButtonClicked = onPositiveButtonClicked,
        )
    }
}

/**
 * Dialog content if the web page was translated.
 *
 * @param translateToLanguages Translation menu items to be shown in the translate to dropdown.
 * @param onFromDropdownSelected Invoked when the user selects an item on the from dropdown.
 * @param onToDropdownSelected Invoked when the user selects an item on the to dropdown.
 * @param onPositiveButtonClicked Invoked when the user clicks on the positive button.
 * @param onNegativeButtonClicked Invoked when the user clicks on the negative button.
 * @param positiveButtonType Can be enabled,disabled or in progress. If it is null, the button will be disabled.
 * @param initialTo Initial "to" language, based on the translation state and page state.
 */
@Composable
private fun DialogContentTranslated(
    translateToLanguages: List<Language>?,
    onFromDropdownSelected: (Language) -> Unit,
    onToDropdownSelected: (Language) -> Unit,
    onPositiveButtonClicked: () -> Unit,
    onNegativeButtonClicked: () -> Unit,
    positiveButtonType: PositiveButtonType? = null,
    initialTo: Language? = null,
) {
    Spacer(modifier = Modifier.height(16.dp))

    TranslationsDialogContent(
        translateToLanguages = translateToLanguages,
        initialTo = initialTo,
        onFromDropdownSelected = onFromDropdownSelected,
        onToDropdownSelected = onToDropdownSelected,
    )

    Spacer(modifier = Modifier.height(16.dp))

    TranslationsDialogActionButtons(
        positiveButtonText = stringResource(id = R.string.translations_bottom_sheet_positive_button),
        negativeButtonText = stringResource(id = R.string.translations_bottom_sheet_negative_button_restore),
        positiveButtonType = positiveButtonType,
        onPositiveButtonClicked = onPositiveButtonClicked,
        onNegativeButtonClicked = onNegativeButtonClicked,
    )
}

/**
 * Dialog content if an [TranslationError] appears during the translation process.
 *
 * @param translationsDialogState The current state of the Translations bottom sheet dialog.
 * @param onFromDropdownSelected Invoked when the user selects an item on the from dropdown.
 * @param onToDropdownSelected Invoked when the user selects an item on the to dropdown.
 * @param onPositiveButtonClicked Invoked when the user clicks on the positive button.
 * @param onNegativeButtonClicked Invoked when the user clicks on the negative button.
 */
@Suppress("LongParameterList")
@Composable
private fun DialogContentAnErrorOccurred(
    translationsDialogState: TranslationsDialogState,
    onFromDropdownSelected: (Language) -> Unit,
    onToDropdownSelected: (Language) -> Unit,
    onPositiveButtonClicked: () -> Unit,
    onNegativeButtonClicked: () -> Unit,
) {
    translationsDialogState.error?.let { translationError ->
        TranslationErrorWarning(
            translationError = translationError,
            documentLangDisplayName = translationsDialogState.documentLangDisplayName,
        )

        Spacer(modifier = Modifier.height(16.dp))

        if (translationError !is TranslationError.CouldNotLoadLanguagesError) {
            TranslationsDialogContent(
                translateFromLanguages = translationsDialogState.fromLanguages,
                translateToLanguages = translationsDialogState.toLanguages,
                initialFrom = translationsDialogState.initialFrom,
                initialTo = translationsDialogState.initialTo,
                translationError = translationError,
                onFromDropdownSelected = onFromDropdownSelected,
                onToDropdownSelected = onToDropdownSelected,
            )
        }

        val negativeButtonTitle =
            if (translationError is TranslationError.LanguageNotSupportedError) {
                stringResource(id = R.string.translations_bottom_sheet_negative_button_error)
            } else {
                stringResource(id = R.string.translations_bottom_sheet_negative_button)
            }

        val positiveButtonTitle =
            if (translationError is TranslationError.CouldNotLoadLanguagesError) {
                stringResource(id = R.string.translations_bottom_sheet_positive_button_error)
            } else {
                stringResource(id = R.string.translations_bottom_sheet_positive_button)
            }

        val positiveButtonType =
            if (translationError is TranslationError.CouldNotLoadLanguagesError) {
                PositiveButtonType.Enabled
            } else {
                translationsDialogState.positiveButtonType
            }

        Spacer(modifier = Modifier.height(16.dp))

        TranslationsDialogActionButtons(
            positiveButtonText = positiveButtonTitle,
            negativeButtonText = negativeButtonTitle,
            positiveButtonType = positiveButtonType,
            onNegativeButtonClicked = onNegativeButtonClicked,
            onPositiveButtonClicked = onPositiveButtonClicked,
        )
    }
}

@Composable
private fun TranslationsDialogContent(
    translateFromLanguages: List<Language>? = null,
    translateToLanguages: List<Language>? = null,
    initialFrom: Language? = null,
    initialTo: Language? = null,
    translationError: TranslationError? = null,
    onFromDropdownSelected: (Language) -> Unit,
    onToDropdownSelected: (Language) -> Unit,
) {
    val allLanguagesList = mutableListOf<Language>()

    with(allLanguagesList) {
        translateToLanguages?.let { addAll(it) }
        translateFromLanguages?.let { addAll(it) }
    }

    val longestLanguageSize = getLongestLanguageWidth(allLanguagesList, FirefoxTheme.typography.body1)

    when (LocalConfiguration.current.orientation) {
        Configuration.ORIENTATION_LANDSCAPE -> {
            TranslationsDialogContentInLandscapeMode(
                longestLanguageSize = longestLanguageSize,
                translateFromLanguages = translateFromLanguages,
                translateToLanguages = translateToLanguages,
                initialFrom = initialFrom,
                initialTo = initialTo,
                translationError = translationError,
                onFromDropdownSelected = onFromDropdownSelected,
                onToDropdownSelected = onToDropdownSelected,
            )
        }

        else -> {
            TranslationsDialogContentInPortraitMode(
                longestLanguageSize = longestLanguageSize,
                translateFromLanguages = translateFromLanguages,
                translateToLanguages = translateToLanguages,
                initialFrom = initialFrom,
                initialTo = initialTo,
                translationError = translationError,
                onFromDropdownSelected = onFromDropdownSelected,
                onToDropdownSelected = onToDropdownSelected,
            )
        }
    }
}

@Composable
private fun TranslationsDialogContentInPortraitMode(
    longestLanguageSize: Dp,
    translateFromLanguages: List<Language>? = null,
    translateToLanguages: List<Language>? = null,
    initialFrom: Language? = null,
    initialTo: Language? = null,
    translationError: TranslationError? = null,
    onFromDropdownSelected: (Language) -> Unit,
    onToDropdownSelected: (Language) -> Unit,
) {
    Column {
        // The LanguageNotSupportedError has a slightly different presentation on this screen.
        val header =
            if (translationError is TranslationError.LanguageNotSupportedError) {
                stringResource(id = R.string.translations_bottom_sheet_translate_from_unsupported_language)
            } else {
                stringResource(id = R.string.translations_bottom_sheet_translate_from)
            }

        translateFromLanguages?.let {
            Dropdown(
                label = header,
                placeholder = stringResource(R.string.translations_bottom_sheet_default_dropdown_selection),
                dropdownItems = getContextMenuItems(
                    translateLanguages = it,
                    selectedLanguage = initialFrom,
                    onClickItem = { language ->
                        onFromDropdownSelected(language)
                    },
                ),
                modifier = Modifier.fillMaxWidth(),
                dropdownMenuTextWidth = longestLanguageSize,
                isInLandscapeMode = false,
            )

            Spacer(modifier = Modifier.height(16.dp))
        }

        if (translationError !is TranslationError.LanguageNotSupportedError) {
            translateToLanguages?.let {
                Dropdown(
                    label = stringResource(id = R.string.translations_bottom_sheet_translate_to),
                    placeholder = stringResource(R.string.translations_bottom_sheet_default_dropdown_selection),
                    dropdownItems = getContextMenuItems(
                        translateLanguages = it,
                        selectedLanguage = initialTo,
                        onClickItem = { language ->
                            onToDropdownSelected(language)
                        },
                    ),
                    modifier = Modifier.fillMaxWidth(),
                    dropdownMenuTextWidth = longestLanguageSize,
                    isInLandscapeMode = false,
                )
            }
        }
    }
}

@Composable
private fun TranslationsDialogContentInLandscapeMode(
    longestLanguageSize: Dp,
    translateFromLanguages: List<Language>? = null,
    translateToLanguages: List<Language>? = null,
    initialFrom: Language? = null,
    initialTo: Language? = null,
    translationError: TranslationError? = null,
    onFromDropdownSelected: (Language) -> Unit,
    onToDropdownSelected: (Language) -> Unit,
) {
    Column {
        Row {
            // The LanguageNotSupportedError has a slightly different presentation on this screen.
            val header =
                if (translationError is TranslationError.LanguageNotSupportedError) {
                    stringResource(id = R.string.translations_bottom_sheet_translate_from_unsupported_language)
                } else {
                    stringResource(id = R.string.translations_bottom_sheet_translate_from)
                }

            translateFromLanguages?.let {
                Dropdown(
                    label = header,
                    placeholder = stringResource(R.string.translations_bottom_sheet_default_dropdown_selection),
                    dropdownItems = getContextMenuItems(
                        translateLanguages = it,
                        selectedLanguage = initialFrom,
                        onClickItem = { language ->
                            onFromDropdownSelected(language)
                        },
                    ),
                    modifier = Modifier.weight(1f),
                    dropdownMenuTextWidth = longestLanguageSize,
                    isInLandscapeMode = true,
                )

                Spacer(modifier = Modifier.width(16.dp))
            }

            if (translationError !is TranslationError.LanguageNotSupportedError) {
                translateToLanguages?.let {
                    Dropdown(
                        label = stringResource(id = R.string.translations_bottom_sheet_translate_to),
                        placeholder = stringResource(R.string.translations_bottom_sheet_default_dropdown_selection),
                        dropdownItems = getContextMenuItems(
                            translateLanguages = it,
                            selectedLanguage = initialTo,
                            onClickItem = { language ->
                                onToDropdownSelected(language)
                            },
                        ),
                        modifier = Modifier.weight(1f),
                        dropdownMenuTextWidth = longestLanguageSize,
                        isInLandscapeMode = true,
                    )
                }
            }
        }
    }
}

@Composable
private fun TranslationsDialogHeader(
    title: String,
    showPageSettings: Boolean,
    onSettingClicked: () -> Unit,
) {
    if (showPageSettings) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.End,
        ) {
            IconButton(
                onClick = { onSettingClicked() },
                contentDescription = stringResource(
                    id = R.string.translation_option_bottom_sheet_title_heading,
                ),
                modifier = Modifier.size(24.dp),
            ) {
                Icon(
                    painter = painterResource(id = iconsR.drawable.mozac_ic_settings_24),
                    contentDescription = null,
                )
            }
        }
    }

    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier.padding(
            top = if (showPageSettings) {
                12.dp
            } else {
                0.dp
            },
        ),
    ) {
        Text(
            text = title,
            modifier = Modifier
                .weight(1f)
                .semantics { heading() },
            style = FirefoxTheme.typography.headline7,
        )
    }
}

@Composable
private fun TranslationErrorWarning(
    translationError: TranslationError,
    documentLangDisplayName: String? = null,
) {
    val modifier = Modifier
        .padding(top = 12.dp)
        .fillMaxWidth()

    when (translationError) {
        is TranslationError.CouldNotTranslateError -> {
            InfoCard(
                description = stringResource(id = R.string.translation_error_could_not_translate_warning_text),
                type = InfoType.Error,
                verticalRowAlignment = Alignment.CenterVertically,
                modifier = modifier,
            )
        }

        is TranslationError.CouldNotLoadLanguagesError -> {
            InfoCard(
                description = stringResource(id = R.string.translation_error_could_not_load_languages_warning_text),
                type = InfoType.Error,
                verticalRowAlignment = Alignment.CenterVertically,
                modifier = modifier,
            )
        }

        is TranslationError.LanguageNotSupportedError -> {
            documentLangDisplayName?.let {
                InfoCard(
                    description = stringResource(
                        id = R.string.translation_error_language_not_supported_warning_text_2,
                        it,
                    ),
                    type = InfoType.Info,
                    verticalRowAlignment = Alignment.CenterVertically,
                    modifier = modifier,
                )
            }
        }

        else -> {}
    }
}

@Composable
private fun TranslationsDialogInfoMessage(
    learnMoreUrl: String,
    onLearnMoreClicked: () -> Unit,
) {
    val learnMoreText =
        stringResource(id = R.string.translations_bottom_sheet_info_message_learn_more)

    val learnMoreState = LinkTextState(
        text = learnMoreText,
        url = learnMoreUrl,
        onClick = { onLearnMoreClicked() },
    )

    Box {
        LinkText(
            text = stringResource(
                R.string.translations_bottom_sheet_info_message,
                learnMoreText,
            ),
            linkTextStates = listOf(learnMoreState),
            style = FirefoxTheme.typography.body2.copy(
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            ),
            linkTextDecoration = TextDecoration.Underline,
        )
    }
}

@Composable
private fun getTranslationsDialogTitle(
    showFirstTime: Boolean = false,
) = if (showFirstTime) {
    stringResource(
        id = R.string.translations_bottom_sheet_title_first_time,
        stringResource(id = R.string.firefox),
    )
} else {
    stringResource(id = R.string.translations_bottom_sheet_title)
}

private fun getContextMenuItems(
    translateLanguages: List<Language>,
    selectedLanguage: Language? = null,
    onClickItem: (Language) -> Unit,
): List<CheckableItem> =
    translateLanguages.mapNotNull { item ->
        item.localizedDisplayName?.let {
            CheckableItem(
                text = Text.String(it),
                isChecked = item == selectedLanguage,
                onClick = {
                    onClickItem(item)
                },
            )
        }
    }

@Composable
private fun TranslationsDialogActionButtons(
    positiveButtonText: String,
    negativeButtonText: String,
    positiveButtonType: PositiveButtonType? = null,
    onPositiveButtonClicked: () -> Unit,
    onNegativeButtonClicked: () -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.End,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        TextButton(
            text = negativeButtonText,
            modifier = Modifier,
            onClick = onNegativeButtonClicked,
        )

        Spacer(modifier = Modifier.width(10.dp))

        when (positiveButtonType) {
            PositiveButtonType.InProgress -> {
                DownloadIndicator(
                    text = positiveButtonText,
                    contentDescription = stringResource(
                        id = R.string.translations_bottom_sheet_translating_in_progress_content_description,
                    ),
                    icon = painterResource(id = iconsR.drawable.mozac_ic_sync_24),
                )
            }

            PositiveButtonType.Enabled -> {
                FilledButton(
                    text = positiveButtonText,
                    modifier = Modifier.wrapContentSize(),
                ) {
                    onPositiveButtonClicked()
                }
            }

            else -> {
                FilledButton(
                    text = positiveButtonText,
                    enabled = false,
                    modifier = Modifier.wrapContentSize(),
                ) {
                    onPositiveButtonClicked()
                }
            }
        }
    }
}

@Composable
private fun getLongestLanguageWidth(languages: List<Language>, style: TextStyle): Dp {
    val textMeasurer = rememberTextMeasurer(cacheSize = languages.size)

    val maxWidth = languages.mapNotNull { it.localizedDisplayName }
        .maxOfOrNull { text ->
            textMeasurer.measure(text = text, style = style).size.width
        }

    return with(LocalDensity.current) { maxWidth?.toDp() ?: 0.dp }
}

@Composable
internal fun getTranslateFromLanguageList(): List<Language> {
    return mutableListOf<Language>().apply {
        add(
            Language(
                code = Locale.ENGLISH.toLanguageTag(),
                localizedDisplayName = Locale.ENGLISH.displayLanguage,
            ),
        )
        add(
            Language(
                code = Locale.FRENCH.toLanguageTag(),
                localizedDisplayName = Locale.FRENCH.displayLanguage,
            ),
        )
        add(
            Language(
                code = Locale.GERMAN.toLanguageTag(),
                localizedDisplayName = Locale.GERMAN.displayLanguage,
            ),
        )
        add(
            Language(
                code = Locale.ITALIAN.toLanguageTag(),
                localizedDisplayName = Locale.ITALIAN.displayLanguage,
            ),
        )
    }
}

@Composable
internal fun getTranslateToLanguageList(): List<Language> {
    return mutableListOf<Language>().apply {
        add(
            Language(
                code = Locale.ENGLISH.toLanguageTag(),
                localizedDisplayName = Locale.ENGLISH.displayLanguage,
            ),
        )
        add(
            Language(
                code = Locale.FRENCH.toLanguageTag(),
                localizedDisplayName = Locale.FRENCH.displayLanguage,
            ),
        )
        add(
            Language(
                code = Locale.GERMAN.toLanguageTag(),
                localizedDisplayName = Locale.GERMAN.displayLanguage,
            ),
        )
        add(
            Language(
                code = Locale.ITALIAN.toLanguageTag(),
                localizedDisplayName = Locale.ITALIAN.displayLanguage,
            ),
        )
    }
}

@Preview
@Composable
private fun TranslationsDialogBottomSheetPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        Surface {
            TranslationsDialogBottomSheet(
                translationsDialogState = TranslationsDialogState(
                    positiveButtonType = PositiveButtonType.Enabled,
                    toLanguages = getTranslateToLanguageList(),
                    fromLanguages = getTranslateFromLanguageList(),
                ),
                learnMoreUrl = "",
                showPageSettings = true,
                showFirstTimeFlow = true,
                onSettingClicked = {},
                onLearnMoreClicked = {},
                onPositiveButtonClicked = {},
                onNegativeButtonClicked = {},
                onFromDropdownSelected = {},
                onToDropdownSelected = {},
            )
        }
    }
}
