/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.compose

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.store.TranslationInfo
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.PreviewThemeProvider
import org.mozilla.fenix.theme.Theme
import mozilla.components.ui.icons.R as iconsR

@Suppress("LongParameterList", "CognitiveComplexMethod")
@Composable
internal fun MoreSettingsSubmenu(
    isPinned: Boolean,
    isInstallable: Boolean,
    isAddToHomeScreenSupported: Boolean,
    hasExternalApp: Boolean,
    externalAppName: String,
    isReaderViewActive: Boolean,
    isWebCompatReporterSupported: Boolean,
    isWebCompatEnabled: Boolean,
    isOpenInAppMenuHighlighted: Boolean,
    translationInfo: TranslationInfo,
    showShortcuts: Boolean,
    isAndroidAutomotiveAvailable: Boolean,
    showSummarization: Boolean,
    onWebCompatReporterClick: () -> Unit,
    onSummarizePageClick: () -> Unit,
    onShortcutsMenuClick: () -> Unit,
    onAddToHomeScreenMenuClick: () -> Unit,
    onSaveToCollectionMenuClick: () -> Unit,
    onSaveAsPDFMenuClick: () -> Unit,
    onPrintMenuClick: () -> Unit,
    onOpenInAppMenuClick: () -> Unit,
) {
    Column(
        verticalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        TranslationSection(
            translationInfo = translationInfo,
            isReaderViewActive = isReaderViewActive,
        )
        SummarizationMenuItem(
            showSummarization = showSummarization,
            onSummarizePageClick = onSummarizePageClick,
        )
        WebCompatReporterMenuItem(
            isWebCompatReporterSupported = isWebCompatReporterSupported,
            isWebCompatEnabled = isWebCompatEnabled,
            onWebCompatReporterClick = onWebCompatReporterClick,
        )
        ShortcutsSection(
            showShortcuts = showShortcuts,
            isPinned = isPinned,
            onShortcutsMenuClick = onShortcutsMenuClick,
        )
        AddToHomeScreenMenuItem(
            isAddToHomeScreenSupported = isAddToHomeScreenSupported,
            isInstallable = isInstallable,
            onAddToHomeScreenMenuClick = onAddToHomeScreenMenuClick,
        )
        SaveToCollectionMenuItem(
            onSaveToCollectionMenuClick = onSaveToCollectionMenuClick,
        )
        OpenInAppMenuItem(
            hasExternalApp = hasExternalApp,
            externalAppName = externalAppName,
            isOpenInAppMenuHighlighted = isOpenInAppMenuHighlighted,
            onOpenInAppMenuClick = onOpenInAppMenuClick,
        )
        SaveAsPdfMenuItem(
            onSaveAsPDFMenuClick = onSaveAsPDFMenuClick,
        )
        PrintMenuItem(
            isAndroidAutomotiveAvailable = isAndroidAutomotiveAvailable,
            onPrintMenuClick = onPrintMenuClick,
        )
    }
}

@Composable
private fun TranslationSection(
    translationInfo: TranslationInfo,
    isReaderViewActive: Boolean,
) {
    if (translationInfo.isTranslationSupported) {
        TranslationMenuItem(
            translationInfo = translationInfo,
            isReaderViewActive = isReaderViewActive,
        )
    }
}

@Composable
private fun SummarizationMenuItem(
    showSummarization: Boolean,
    onSummarizePageClick: () -> Unit,
) {
    if (showSummarization) {
        MenuItem(
            label = stringResource(id = R.string.browser_menu_summarize_page),
            beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_lightning_24),
            onClick = onSummarizePageClick,
        )
    }
}

@Composable
private fun WebCompatReporterMenuItem(
    isWebCompatReporterSupported: Boolean,
    isWebCompatEnabled: Boolean,
    onWebCompatReporterClick: () -> Unit,
) {
    if (isWebCompatReporterSupported) {
        MenuItem(
            label = stringResource(id = R.string.browser_menu_webcompat_reporter_2),
            beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_lightbulb_24),
            state = if (isWebCompatEnabled) MenuItemState.ENABLED else MenuItemState.DISABLED,
            onClick = onWebCompatReporterClick,
        )
    }
}

@Composable
private fun ShortcutsSection(
    showShortcuts: Boolean,
    isPinned: Boolean,
    onShortcutsMenuClick: () -> Unit,
) {
    if (showShortcuts) {
        ShortcutsMenuItem(
            isPinned = isPinned,
            onShortcutsMenuClick = onShortcutsMenuClick,
        )
    }
}

@Composable
private fun AddToHomeScreenMenuItem(
    isAddToHomeScreenSupported: Boolean,
    isInstallable: Boolean,
    onAddToHomeScreenMenuClick: () -> Unit,
) {
    if (isAddToHomeScreenSupported) {
        MenuItem(
            label = if (isInstallable) {
                stringResource(id = R.string.browser_menu_add_app_to_homescreen)
            } else {
                stringResource(id = R.string.browser_menu_add_to_homescreen)
            },
            beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_add_to_homescreen_24),
            onClick = onAddToHomeScreenMenuClick,
        )
    }
}

@Composable
private fun SaveToCollectionMenuItem(
    onSaveToCollectionMenuClick: () -> Unit,
) {
    MenuItem(
        label = stringResource(id = R.string.browser_menu_save_to_collection_2),
        beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_collection_24),
        onClick = onSaveToCollectionMenuClick,
    )
}

@Composable
private fun OpenInAppMenuItem(
    hasExternalApp: Boolean,
    externalAppName: String,
    isOpenInAppMenuHighlighted: Boolean,
    onOpenInAppMenuClick: () -> Unit,
) {
    if (hasExternalApp) {
        MenuItem(
            label = if (externalAppName != "") {
                stringResource(id = R.string.browser_menu_open_in_fenix, externalAppName)
            } else {
                stringResource(id = R.string.browser_menu_open_app_link)
            },
            beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_more_grid_24),
            isBeforeIconHighlighted = isOpenInAppMenuHighlighted,
            state = MenuItemState.ENABLED,
            onClick = onOpenInAppMenuClick,
        )
    } else {
        MenuItem(
            label = stringResource(id = R.string.browser_menu_open_app_link),
            beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_more_grid_24),
            state = MenuItemState.DISABLED,
        )
    }
}

@Composable
private fun SaveAsPdfMenuItem(
    onSaveAsPDFMenuClick: () -> Unit,
) {
    MenuItem(
        label = stringResource(id = R.string.browser_menu_save_as_pdf_2),
        beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_save_file_24),
        onClick = onSaveAsPDFMenuClick,
    )
}

@Composable
private fun PrintMenuItem(
    isAndroidAutomotiveAvailable: Boolean,
    onPrintMenuClick: () -> Unit,
) {
    if (!isAndroidAutomotiveAvailable) {
        MenuItem(
            label = stringResource(id = R.string.browser_menu_print_2),
            beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_print_24),
            onClick = onPrintMenuClick,
        )
    }
}

@Composable
private fun TranslationMenuItem(
    translationInfo: TranslationInfo,
    isReaderViewActive: Boolean,
) {
    if (translationInfo.isTranslated) {
        val state = if (isReaderViewActive || translationInfo.isPdf) MenuItemState.DISABLED else MenuItemState.ACTIVE
        MenuItem(
            label = stringResource(id = R.string.browser_menu_translated),
            beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_translate_active_24),
            state = state,
            onClick = translationInfo.onTranslatePageMenuClick,
        ) {
            Badge(
                badgeText = translationInfo.translatedLanguage,
                state = state,
            )
        }
    } else {
        MenuItem(
            label = stringResource(id = R.string.browser_menu_translate_page_2),
            beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_translate_24),
            state = if (isReaderViewActive || translationInfo.isPdf) MenuItemState.DISABLED else MenuItemState.ENABLED,
            onClick = translationInfo.onTranslatePageMenuClick,
        )
    }
}

@Composable
private fun ShortcutsMenuItem(
    isPinned: Boolean,
    onShortcutsMenuClick: () -> Unit,
) {
    MenuItem(
        label = if (isPinned) {
            stringResource(id = R.string.browser_menu_remove_from_shortcuts)
        } else {
            stringResource(id = R.string.browser_menu_add_to_shortcuts)
        },
        beforeIconPainter = if (isPinned) {
            painterResource(id = iconsR.drawable.mozac_ic_pin_fill_24)
        } else {
            painterResource(id = iconsR.drawable.mozac_ic_pin_24)
        },
        state = if (isPinned) {
            MenuItemState.ACTIVE
        } else {
            MenuItemState.ENABLED
        },
        onClick = onShortcutsMenuClick,
    )
}

@Preview
@Composable
private fun MoreSettingsSubmenuPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        Column(
            modifier = Modifier
                .background(color = MaterialTheme.colorScheme.surface)
                .padding(all = FirefoxTheme.layout.space.static200),
        ) {
            MenuGroup {
                MoreSettingsSubmenu(
                    isPinned = true,
                    isInstallable = true,
                    isAddToHomeScreenSupported = true,
                    hasExternalApp = true,
                    externalAppName = "Pocket",
                    isReaderViewActive = false,
                    isWebCompatReporterSupported = true,
                    isWebCompatEnabled = true,
                    isOpenInAppMenuHighlighted = false,
                    translationInfo = TranslationInfo(
                        isTranslationSupported = true,
                        isPdf = false,
                        isTranslated = true,
                        translatedLanguage = "English",
                        onTranslatePageMenuClick = {},
                    ),
                    showShortcuts = true,
                    isAndroidAutomotiveAvailable = false,
                    showSummarization = true,
                    onWebCompatReporterClick = {},
                    onSummarizePageClick = {},
                    onShortcutsMenuClick = {},
                    onAddToHomeScreenMenuClick = {},
                    onSaveToCollectionMenuClick = {},
                    onSaveAsPDFMenuClick = {},
                    onPrintMenuClick = {},
                    onOpenInAppMenuClick = {},
                )
            }
        }
    }
}

@Preview
@Composable
private fun MoreSettingsSubmenuDisabledOpenPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        Column(
            modifier = Modifier
                .background(color = MaterialTheme.colorScheme.surface)
                .padding(all = FirefoxTheme.layout.space.static200),
        ) {
            MenuGroup {
                MoreSettingsSubmenu(
                    isPinned = false,
                    isInstallable = true,
                    isAddToHomeScreenSupported = false,
                    hasExternalApp = false,
                    externalAppName = "Pocket",
                    isReaderViewActive = false,
                    isWebCompatReporterSupported = true,
                    isWebCompatEnabled = true,
                    isOpenInAppMenuHighlighted = true,
                    translationInfo = TranslationInfo(
                        isTranslationSupported = true,
                        isPdf = false,
                        isTranslated = false,
                        translatedLanguage = "English",
                        onTranslatePageMenuClick = {},
                    ),
                    showShortcuts = true,
                    isAndroidAutomotiveAvailable = false,
                    showSummarization = false,
                    onWebCompatReporterClick = {},
                    onSummarizePageClick = {},
                    onShortcutsMenuClick = {},
                    onAddToHomeScreenMenuClick = {},
                    onSaveToCollectionMenuClick = {},
                    onSaveAsPDFMenuClick = {},
                    onPrintMenuClick = {},
                    onOpenInAppMenuClick = {},
                )
            }
        }
    }
}
