/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.compose

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.wrapContentWidth
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.theme.information
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.store.SummarizationMenuState
import org.mozilla.fenix.components.menu.store.TranslationInfo
import org.mozilla.fenix.compose.StatusBadge
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
    summarizationMenuState: SummarizationMenuState,
    isPrivate: Boolean,
    onWebCompatReporterClick: () -> Unit,
    onSummarizePageMenuExposed: () -> Unit,
    onSummarizePageClick: () -> Unit,
    onShortcutsMenuClick: () -> Unit,
    onAddToHomeScreenMenuClick: () -> Unit,
    onSaveToCollectionMenuClick: () -> Unit,
    onSaveAsPDFMenuClick: () -> Unit,
    onPrintMenuClick: () -> Unit,
    onOpenInAppMenuClick: () -> Unit,
    onMoveToNonPrivateTabMenuClick: () -> Unit,
) {
    Column(
        verticalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        TranslationSection(
            translationInfo = translationInfo,
            isReaderViewActive = isReaderViewActive,
        )
        SummarizationMenuItem(
            summarizationMenuState = summarizationMenuState,
            onSummarizePageMenuExposed = onSummarizePageMenuExposed,
            onSummarizePageClick = onSummarizePageClick,
        )
        MoveToNonPrivateTabMenuItem(
            isPrivate = isPrivate,
            onMoveToNonPrivateTabMenuClick = onMoveToNonPrivateTabMenuClick,
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

/**
 * Summarization menu item.
 *
 * @param summarizationMenuState The state of the summarization menu.
 * @param onSummarizePageMenuExposed A callback to be executed when the menu is exposed to the user.
 * it will be used to know when to remove the highlight.
 * @param onSummarizePageClick A callback to be executed when the menu item is clicked.
 */
@Composable
private fun SummarizationMenuItem(
    summarizationMenuState: SummarizationMenuState,
    onSummarizePageMenuExposed: () -> Unit,
    onSummarizePageClick: () -> Unit,
) {
    if (summarizationMenuState.visible) {
        LaunchedEffect(Unit) {
            if (summarizationMenuState.highlighted) {
                onSummarizePageMenuExposed()
            }
        }
        val state: MenuItemState = if (summarizationMenuState.enabled) {
            MenuItemState.ENABLED
        } else {
            MenuItemState.DISABLED
        }

        val containerColor = if (summarizationMenuState.enabled) {
            MaterialTheme.colorScheme.information
        } else {
            MaterialTheme.colorScheme.information.copy(alpha = 0.38f)
        }

        MenuItem(
            label = stringResource(id = R.string.browser_menu_summarize_page),
            labelModifier = Modifier.wrapContentWidth(),
            beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_lightning_24),
            isBeforeIconHighlighted = summarizationMenuState.highlighted,
            onClick = onSummarizePageClick,
            state = state,
            afterContent = {
                if (summarizationMenuState.showNewFeatureBadge) {
                    StatusBadge(
                        containerColor = containerColor,
                        contentColor = MaterialTheme.colorScheme.onPrimary,
                        status = stringResource(R.string.browser_menu_summarize_page_badge),
                    )
                }
            },
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
private fun MoveToNonPrivateTabMenuItem(
    isPrivate: Boolean,
    onMoveToNonPrivateTabMenuClick: () -> Unit,
) {
    if (isPrivate) {
        MenuItem(
            label = stringResource(id = R.string.browser_menu_move_to_non_private_tab),
            beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_open_in),
            onClick = onMoveToNonPrivateTabMenuClick,
        )
    }
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
                    summarizationMenuState = SummarizationMenuState.Default.copy(
                        visible = true,
                        highlighted = true,
                        showNewFeatureBadge = true,
                    ),
                    isPrivate = true,
                    onWebCompatReporterClick = {},
                    onSummarizePageMenuExposed = {},
                    onSummarizePageClick = {},
                    onShortcutsMenuClick = {},
                    onAddToHomeScreenMenuClick = {},
                    onSaveToCollectionMenuClick = {},
                    onSaveAsPDFMenuClick = {},
                    onPrintMenuClick = {},
                    onOpenInAppMenuClick = {},
                    onMoveToNonPrivateTabMenuClick = {},
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
                    summarizationMenuState = SummarizationMenuState.Default,
                    isPrivate = false,
                    onWebCompatReporterClick = {},
                    onSummarizePageMenuExposed = {},
                    onSummarizePageClick = {},
                    onShortcutsMenuClick = {},
                    onAddToHomeScreenMenuClick = {},
                    onSaveToCollectionMenuClick = {},
                    onSaveAsPDFMenuClick = {},
                    onPrintMenuClick = {},
                    onOpenInAppMenuClick = {},
                    onMoveToNonPrivateTabMenuClick = {},
                )
            }
        }
    }
}
