/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.translations.preferences.downloadlanguages

import androidx.compose.foundation.LocalIndication
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.InfoCard
import mozilla.components.compose.base.InfoType
import mozilla.components.compose.base.LinkText
import mozilla.components.compose.base.LinkTextState
import mozilla.components.concept.engine.translate.Language
import mozilla.components.concept.engine.translate.LanguageModel
import mozilla.components.concept.engine.translate.ModelState
import mozilla.components.concept.engine.translate.TranslationError
import mozilla.components.feature.downloads.DefaultFileSizeFormatter
import mozilla.components.feature.downloads.FileSizeFormatter
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.PreviewThemeProvider
import org.mozilla.fenix.theme.Theme
import org.mozilla.fenix.translations.DownloadIconIndicator
import org.mozilla.fenix.translations.DownloadInProgressIndicator
import java.util.Locale
import mozilla.components.ui.icons.R as iconsR

/**
 * Firefox Download Languages preference screen.
 *
 * @param downloadLanguageItemPreferences List of [DownloadLanguageItemPreference]s that needs to be displayed.
 * @param learnMoreUrl The learn more link for translations website.
 * @param fileSizeFormatter [FileSizeFormatter] used to format the size of the file item.
 * @param downloadLanguagesError  If a translation error occurs.
 * @param onLearnMoreClicked Invoked when the user clicks on the "Learn More" button.
 * @param onItemClick Invoked when the user clicks on the language item.
 */
@Suppress("LongMethod", "CyclomaticComplexMethod", "CognitiveComplexMethod")
@Composable
fun DownloadLanguagesPreference(
    downloadLanguageItemPreferences: List<DownloadLanguageItemPreference>,
    learnMoreUrl: String,
    fileSizeFormatter: FileSizeFormatter,
    downloadLanguagesError: TranslationError? = null,
    onLearnMoreClicked: () -> Unit,
    onItemClick: (DownloadLanguageItemPreference) -> Unit,
) {
    val downloadedItems = downloadLanguageItemPreferences.filter {
        (
            it.languageModel.status == ModelState.DOWNLOADED ||
                it.languageModel.status == ModelState.ERROR_DELETION
            ) &&
            it.type != DownloadLanguageItemTypePreference.AllLanguages
    }

    val notDownloadedItems = downloadLanguageItemPreferences.filter {
        (
            it.languageModel.status == ModelState.NOT_DOWNLOADED ||
                it.languageModel.status == ModelState.ERROR_DOWNLOAD
            ) &&
            it.type != DownloadLanguageItemTypePreference.AllLanguages
    }

    val downloadInProgressItems = downloadLanguageItemPreferences.filter {
        it.languageModel.status == ModelState.DOWNLOAD_IN_PROGRESS &&
            it.type != DownloadLanguageItemTypePreference.AllLanguages
    }

    val deleteInProgressItems = downloadLanguageItemPreferences.filter {
        it.languageModel.status == ModelState.DELETION_IN_PROGRESS &&
            it.type != DownloadLanguageItemTypePreference.AllLanguages
    }

    var allLanguagesItemDownloaded: DownloadLanguageItemPreference? = null
    if (downloadLanguageItemPreferences.any {
            it.type == DownloadLanguageItemTypePreference.AllLanguages &&
                it.languageModel.status == ModelState.DOWNLOADED
        }
    ) {
        allLanguagesItemDownloaded = downloadLanguageItemPreferences.last {
            it.type == DownloadLanguageItemTypePreference.AllLanguages &&
                it.languageModel.status == ModelState.DOWNLOADED
        }
    }

    // Items that are in progress or not downloaded at all.
    val itemsNotDownloaded = mutableListOf<DownloadLanguageItemPreference>()
    itemsNotDownloaded.addAll(downloadInProgressItems)
    itemsNotDownloaded.addAll(deleteInProgressItems)
    itemsNotDownloaded.addAll(notDownloadedItems)
    itemsNotDownloaded.sortBy { it.languageModel.language?.localizedDisplayName }

    var pivotLanguage: DownloadLanguageItemPreference? = null
    if (downloadLanguageItemPreferences.any { it.type == DownloadLanguageItemTypePreference.PivotLanguage }) {
        pivotLanguage = downloadLanguageItemPreferences.last {
            it.type == DownloadLanguageItemTypePreference.PivotLanguage
        }
    }

    Surface {
        LazyColumn {
            item {
                DownloadLanguagesHeaderPreference(
                    learnMoreUrl = learnMoreUrl,
                    onLearnMoreClicked = onLearnMoreClicked,
                )
            }

            if (downloadLanguagesError != null) {
                item {
                    DownloadLanguagesErrorWarning(
                        stringResource(id = R.string.download_languages_fetch_error_warning_text),
                    )
                }
            }

            if (
                allLanguagesItemDownloaded != null ||
                pivotLanguage?.languageModel?.status == ModelState.DOWNLOADED
            ) {
                item {
                    DownloadLanguagesHeader(
                        stringResource(
                            id = R.string.download_languages_available_languages_preference,
                        ),
                    )
                }
            }

            allLanguagesItemDownloaded?.let {
                item {
                    LanguageItemPreference(
                        item = allLanguagesItemDownloaded,
                        fileSizeFormatter = fileSizeFormatter,
                        onItemClick = onItemClick,
                    )
                }
            }

            items(downloadedItems) { item: DownloadLanguageItemPreference ->
                LanguageItemPreference(
                    item = item,
                    fileSizeFormatter = fileSizeFormatter,
                    onItemClick = onItemClick,
                )
            }

            if (pivotLanguage?.languageModel?.status == ModelState.NOT_DOWNLOADED ||
                shouldShowDownloadLanguagesHeader(
                    itemsNotDownloaded = itemsNotDownloaded,
                    deleteInProgressItems = deleteInProgressItems,
                    notDownloadedItems = notDownloadedItems,
                )
            ) {
                if (pivotLanguage?.languageModel?.status == ModelState.DOWNLOADED ||
                    allLanguagesItemDownloaded != null
                ) {
                    item {
                        HorizontalDivider(Modifier.padding(top = 8.dp, bottom = 8.dp))
                    }
                }

                item {
                    DownloadLanguagesHeader(
                        stringResource(
                            id = R.string.download_language_header_preference,
                        ),
                    )
                }
            }

            itemsNotDownloaded.forEach { item ->
                item {
                    LanguageItemPreference(
                        item = item,
                        fileSizeFormatter = fileSizeFormatter,
                        onItemClick = onItemClick,
                    )
                }
            }
        }
    }

    // The pivot model may be deleted when all of the other models are deleted and it may
    // always be downloaded
    pivotLanguage?.enabled = downloadedItems.size == 1 ||
        pivotLanguage.languageModel.status == ModelState.NOT_DOWNLOADED
}

@Composable
private fun DownloadLanguagesErrorWarning(title: String) {
    val modifier = Modifier
        .fillMaxWidth()
        .padding(start = 72.dp, end = 16.dp)
        .defaultMinSize(minHeight = 56.dp)
        .wrapContentHeight()

    InfoCard(
        description = title,
        type = InfoType.Warning,
        verticalRowAlignment = Alignment.CenterVertically,
        modifier = modifier,
    )
}

private fun shouldShowDownloadLanguagesHeader(
    itemsNotDownloaded: List<DownloadLanguageItemPreference>,
    deleteInProgressItems: List<DownloadLanguageItemPreference>,
    notDownloadedItems: List<DownloadLanguageItemPreference>,
): Boolean {
    return itemsNotDownloaded.isNotEmpty() ||
        deleteInProgressItems.isNotEmpty() ||
        notDownloadedItems.isNotEmpty()
}

@Composable
private fun DownloadLanguagesHeader(title: String) {
    Text(
        text = title,
        modifier = Modifier
            .fillMaxWidth()
            .padding(start = 72.dp, end = 16.dp)
            .semantics { heading() }
            .defaultMinSize(minHeight = 36.dp)
            .wrapContentHeight(),
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        style = FirefoxTheme.typography.headline8,
    )
}

@Composable
private fun LanguageItemPreference(
    item: DownloadLanguageItemPreference,
    fileSizeFormatter: FileSizeFormatter,
    onItemClick: (DownloadLanguageItemPreference) -> Unit,
) {
    val description: String =
        if (
            item.type == DownloadLanguageItemTypePreference.PivotLanguage &&
            item.languageModel.status == ModelState.DOWNLOADED &&
            !item.enabled
        ) {
            stringResource(id = R.string.download_languages_default_system_language_require_preference)
        } else {
            val size = item.languageModel.size ?: 0L
            fileSizeFormatter.formatSizeInBytes(size)
        }

    val label = if (item.type == DownloadLanguageItemTypePreference.AllLanguages) {
        if (item.languageModel.status == ModelState.NOT_DOWNLOADED) {
            stringResource(id = R.string.download_language_all_languages_item_preference)
        } else {
            stringResource(id = R.string.download_language_all_languages_item_preference_to_delete)
        }
    } else {
        item.languageModel.language?.localizedDisplayName
    }

    val contentDescription =
        downloadLanguageItemContentDescriptionPreference(
            item = item,
            fileSizeFormatter = fileSizeFormatter,
            label = label,
            itemDescription = description,
        )

    label?.let {
        TextListItemInlineDescription(
            label = it,
            modifier = Modifier
                .padding(
                    start = 56.dp,
                )
                .clearAndSetSemantics {
                    role = Role.Button
                    this.contentDescription = contentDescription
                }
                .defaultMinSize(minHeight = 56.dp)
                .wrapContentHeight(),
            description = description,
            enabled = item.enabled,
            onClick = { onItemClick(item) },
            icon = {
                IconDownloadLanguageItemPreference(
                    item = item,
                )
            },
        )
    }

    if (item.languageModel.status == ModelState.ERROR_DOWNLOAD) {
        item.languageModel.language?.localizedDisplayName?.let {
            DownloadLanguagesErrorWarning(
                stringResource(
                    R.string.download_languages_error_warning_text,
                    it,
                ),
            )
        }
    } else if (item.languageModel.status == ModelState.ERROR_DELETION) {
        item.languageModel.language?.localizedDisplayName?.let {
            DownloadLanguagesErrorWarning(
                stringResource(
                    R.string.download_languages_delete_error_warning_text,
                    it,
                ),
            )
        }
    }
}

@Composable
private fun DownloadLanguagesHeaderPreference(
    learnMoreUrl: String,
    onLearnMoreClicked: () -> Unit,
) {
    val learnMoreText =
        stringResource(id = R.string.download_languages_header_learn_more_preference)
    val learnMoreState = LinkTextState(
        text = learnMoreText,
        url = learnMoreUrl,
        onClick = { onLearnMoreClicked() },
    )
    Box(
        modifier = Modifier
            .padding(
                start = 72.dp,
                top = 6.dp,
                bottom = 6.dp,
                end = 16.dp,
            )
            .semantics { heading() },
    ) {
        LinkText(
            text = stringResource(
                R.string.download_languages_header_preference,
                learnMoreText,
            ),
            linkTextStates = listOf(learnMoreState),
            style = FirefoxTheme.typography.body1.copy(
                color = MaterialTheme.colorScheme.onSurface,
            ),
            linkTextDecoration = TextDecoration.Underline,
        )
    }

    Spacer(modifier = Modifier.size(8.dp))
}

@Composable
private fun downloadLanguageItemContentDescriptionPreference(
    item: DownloadLanguageItemPreference,
    fileSizeFormatter: FileSizeFormatter,
    label: String? = null,
    itemDescription: String,
): String {
    val contentDescription: String

    when (item.languageModel.status) {
        ModelState.DOWNLOADED, ModelState.ERROR_DELETION -> {
            contentDescription =
                "$label $itemDescription" + stringResource(
                    id = R.string.download_languages_item_content_description_downloaded_state,
                )
        }

        ModelState.NOT_DOWNLOADED, ModelState.ERROR_DOWNLOAD -> {
            contentDescription =
                "$label $itemDescription " + stringResource(
                    id = R.string.download_languages_item_content_description_not_downloaded_state,
                )
        }

        ModelState.DELETION_IN_PROGRESS -> {
            contentDescription =
                "$label $itemDescription " + stringResource(
                    id = R.string.download_languages_item_content_description_delete_in_progress_state,
                )
        }

        ModelState.DOWNLOAD_IN_PROGRESS -> {
            contentDescription =
                stringResource(
                    id = R.string.download_languages_item_content_description_download_in_progress_state,
                    item.languageModel.language?.localizedDisplayName ?: "",
                    fileSizeFormatter.formatSizeInBytes(item.languageModel.size ?: 0L),
                )
        }
    }
    return contentDescription
}

@Composable
private fun IconDownloadLanguageItemPreference(
    item: DownloadLanguageItemPreference,
) {
    when (item.languageModel.status) {
        ModelState.DOWNLOADED, ModelState.ERROR_DELETION -> {
            if (
                item.enabled
            ) {
                Icon(
                    painter = painterResource(
                        id = iconsR.drawable.mozac_ic_delete_24,
                    ),
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }

        ModelState.NOT_DOWNLOADED, ModelState.ERROR_DOWNLOAD -> {
            Icon(
                painter = painterResource(
                    id = iconsR.drawable.mozac_ic_download_24,
                ),
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }

        ModelState.DOWNLOAD_IN_PROGRESS -> {
            DownloadInProgressIndicator()
        }

        ModelState.DELETION_IN_PROGRESS -> {
            DownloadIconIndicator(
                icon = painterResource(id = iconsR.drawable.mozac_ic_sync_24),
            )
        }
    }
}

/**
 * List item used to display a label with description text (right next to the label) and
 * an [IconButton] at the end.
 *
 * @param label The label in the list item.
 * @param modifier [Modifier] to be applied to the layout.
 * @param description An description text right next the label.
 * @param enabled Controls the enabled state. When false, onClick,
 * and this modifier will appear disabled for accessibility services.
 * @param onClick Called when the user clicks on the item.
 * @param icon Composable for adding Icon UI.
 */
@Composable
private fun TextListItemInlineDescription(
    label: String,
    modifier: Modifier = Modifier,
    description: String,
    enabled: Boolean = true,
    onClick: (() -> Unit),
    icon: @Composable RowScope.() -> Unit = {},
) {
    Row(
        modifier = Modifier
            .defaultMinSize(minHeight = 56.dp)
            .clickable(
                interactionSource = remember { MutableInteractionSource() },
                indication = LocalIndication.current,
                enabled = enabled,
            ) { onClick.invoke() },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(
            modifier = modifier
                .padding(horizontal = 16.dp, vertical = 6.dp)
                .weight(1f),
        ) {
            val text = stringResource(
                R.string.download_languages_language_item_preference,
                label,
                description,
            )

            Text(
                buildAnnotatedString {
                    withStyle(style = SpanStyle(color = MaterialTheme.colorScheme.onSurface)) {
                        append(label)
                    }
                    withStyle(style = SpanStyle(color = MaterialTheme.colorScheme.onSurfaceVariant)) {
                        append(text.substringAfter(label))
                    }
                },
                style = FirefoxTheme.typography.subtitle1,
            )
        }

        IconButton(
            onClick = { onClick.invoke() },
            enabled = enabled,
            modifier = Modifier
                .padding(end = 16.dp)
                .size(30.dp)
                .clearAndSetSemantics {},
        ) {
            icon()
        }
    }
}

@Suppress("MagicNumber", "LongMethod")
@Composable
internal fun getLanguageListPreference(): List<DownloadLanguageItemPreference> {
    return mutableListOf<DownloadLanguageItemPreference>().apply {
        add(
            DownloadLanguageItemPreference(
                languageModel = LanguageModel(
                    language = Language(Locale.FRENCH.toLanguageTag(), Locale.FRENCH.displayName),
                    status = ModelState.DOWNLOADED,
                    size = 100000L,
                ),
                type = DownloadLanguageItemTypePreference.GeneralLanguage,
            ),
        )
        add(
            DownloadLanguageItemPreference(
                languageModel = LanguageModel(
                    language = Language(Locale.GERMAN.toLanguageTag(), Locale.GERMAN.displayName),
                    status = ModelState.NOT_DOWNLOADED,
                    size = 1000L,
                ),
                type = DownloadLanguageItemTypePreference.GeneralLanguage,
            ),
        )
        add(
            DownloadLanguageItemPreference(
                languageModel = LanguageModel(
                    language = Language(Locale.ITALIAN.toLanguageTag(), Locale.ITALIAN.displayName),
                    status = ModelState.DOWNLOAD_IN_PROGRESS,
                    size = 1000000L,
                ),
                type = DownloadLanguageItemTypePreference.GeneralLanguage,
            ),
        )
        add(
            DownloadLanguageItemPreference(
                languageModel = LanguageModel(
                    language = Language(Locale.ENGLISH.toLanguageTag(), Locale.ENGLISH.displayName),
                    status = ModelState.DELETION_IN_PROGRESS,
                    size = 1000000000L,
                ),
                type = DownloadLanguageItemTypePreference.GeneralLanguage,
            ),
        )
        add(
            DownloadLanguageItemPreference(
                languageModel = LanguageModel(
                    status = ModelState.NOT_DOWNLOADED,
                    size = 10000L,
                ),
                type = DownloadLanguageItemTypePreference.AllLanguages,
            ),
        )
        add(
            DownloadLanguageItemPreference(
                languageModel = LanguageModel(
                    language = Language(Locale.FRENCH.toLanguageTag(), Locale.FRENCH.displayName),
                    status = ModelState.ERROR_DELETION,
                    size = 10000L,
                ),
                type = DownloadLanguageItemTypePreference.PivotLanguage,
            ),
        )
        add(
            DownloadLanguageItemPreference(
                languageModel = LanguageModel(
                    language = Language(Locale.CHINESE.toLanguageTag(), Locale.CHINESE.displayName),
                    status = ModelState.ERROR_DOWNLOAD,
                    size = 10000L,
                ),
                type = DownloadLanguageItemTypePreference.PivotLanguage,
            ),
        )
    }
}

@Preview
@Composable
private fun DownloadLanguagesPreferencePreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        DownloadLanguagesPreference(
            downloadLanguageItemPreferences = getLanguageListPreference(),
            learnMoreUrl = "https://www.mozilla.org",
            fileSizeFormatter = DefaultFileSizeFormatter(LocalContext.current),
            onLearnMoreClicked = {},
            onItemClick = {},
        )
    }
}
