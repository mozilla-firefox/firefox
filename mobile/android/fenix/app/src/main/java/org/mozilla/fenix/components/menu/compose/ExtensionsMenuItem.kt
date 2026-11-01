/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.compose

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.painter.BitmapPainter
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CollectionInfo
import androidx.compose.ui.semantics.collectionInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.theme.ThemedValue
import mozilla.components.compose.base.theme.ThemedValueProvider
import mozilla.components.feature.addons.Addon
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.menu.MenuDialogTestTag.EXTENSIONS
import org.mozilla.fenix.components.menu.MenuDialogTestTag.EXTENSIONS_OPTION_CHEVRON
import org.mozilla.fenix.components.menu.store.WebExtensionMenuItem
import org.mozilla.fenix.theme.FirefoxTheme

@Suppress("LongParameterList")
@Composable
internal fun ExtensionsMenuItem(
    inCustomTab: Boolean,
    isExtensionsProcessDisabled: Boolean,
    isExtensionsExpanded: Boolean,
    isAllWebExtensionsDisabled: Boolean,
    webExtensionMenuCount: Int,
    extensionsMenuItemDescription: String?,
    pendingExtensionUpdatesDescription: String? = null,
    onExtensionsMenuClick: () -> Unit,
    extensionSubmenu: @Composable () -> Unit,
) {
    val stateDescription =
        stateDescriptionForExtensions(
            extensionsMenuItemDescription = extensionsMenuItemDescription,
            isExtensionsExpanded = isExtensionsExpanded,
        )

    val beforeIconPainter = beforeIconPainterForExtensions(isExtensionsProcessDisabled = isExtensionsProcessDisabled)

    val descriptionState =
        if (pendingExtensionUpdatesDescription != null) {
            MenuItemState.WARNING
        } else {
            descriptionStateForExtensions(isExtensionsProcessDisabled = isExtensionsProcessDisabled)
        }

    val state =
        menuItemStateForExtensions(
            inCustomTab = inCustomTab,
            isExtensionsProcessDisabled = isExtensionsProcessDisabled,
            isAllWebExtensionsDisabled = isAllWebExtensionsDisabled,
        )

    Column {
        MenuItem(
            label = stringResource(id = R.string.browser_menu_extensions),
            description = pendingExtensionUpdatesDescription ?: extensionsMenuItemDescription,
            maxDescriptionLines = 1,
            stateDescription = stateDescription,
            beforeIconPainter = beforeIconPainter,
            onClick = onExtensionsMenuClick,
            descriptionState = descriptionState,
            modifier =
                Modifier.semantics {
                    testTag = EXTENSIONS
                    testTagsAsResourceId = true
                },
            state = state,
        ) {
            ExtensionsMenuTrailingContent(
                inCustomTab = inCustomTab,
                isExtensionsProcessDisabled = isExtensionsProcessDisabled,
                isAllWebExtensionsDisabled = isAllWebExtensionsDisabled,
                isExtensionsExpanded = isExtensionsExpanded,
                webExtensionMenuCount = webExtensionMenuCount,
                extensionsMenuItemDescription = extensionsMenuItemDescription,
            )
        }

        ExpandableMenuItemAnimation(
            isExpanded = isExtensionsExpanded,
            content = extensionSubmenu,
        )
    }
}

@Composable
private fun ExtensionsMenuTrailingContent(
    inCustomTab: Boolean,
    isExtensionsProcessDisabled: Boolean,
    isAllWebExtensionsDisabled: Boolean,
    isExtensionsExpanded: Boolean,
    webExtensionMenuCount: Int,
    extensionsMenuItemDescription: String?,
) {
    if (extensionsMenuItemDescription == null) {
        return
    }

    if (isExtensionsProcessDisabled || isAllWebExtensionsDisabled) {
        if (!inCustomTab) {
            Icon(
                painter = painterResource(id = iconsR.drawable.mozac_ic_settings_24),
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        return
    }

    NumberedChevronBadge(
        count = webExtensionMenuCount,
        isExpanded = isExtensionsExpanded,
    )
}

private fun stateDescriptionForExtensions(
    extensionsMenuItemDescription: String?,
    isExtensionsExpanded: Boolean,
): String =
    when {
        extensionsMenuItemDescription == null -> ""
        isExtensionsExpanded -> "Expanded"
        else -> "Collapsed"
    }

@Composable
private fun beforeIconPainterForExtensions(isExtensionsProcessDisabled: Boolean) =
    when {
        isExtensionsProcessDisabled -> painterResource(id = iconsR.drawable.mozac_ic_extension_warning_multicolor_24)
        else -> painterResource(id = iconsR.drawable.mozac_ic_extension_24)
    }

private fun descriptionStateForExtensions(isExtensionsProcessDisabled: Boolean): MenuItemState =
    when (isExtensionsProcessDisabled) {
        true -> MenuItemState.DISABLED
        else -> MenuItemState.ENABLED
    }

private fun menuItemStateForExtensions(
    inCustomTab: Boolean,
    isExtensionsProcessDisabled: Boolean,
    isAllWebExtensionsDisabled: Boolean,
): MenuItemState =
    when {
        inCustomTab && (isExtensionsProcessDisabled || isAllWebExtensionsDisabled) -> MenuItemState.DISABLED
        isExtensionsProcessDisabled -> MenuItemState.CRITICAL
        else -> MenuItemState.ENABLED
    }

@Composable
private fun NumberedChevronBadge(
    count: Int,
    isExpanded: Boolean,
) {
    Row(
        modifier =
            Modifier.background(
                    color = MaterialTheme.colorScheme.surfaceContainerHigh,
                    shape = MaterialTheme.shapes.large,
                )
                .padding(
                    start = if (count > 0) 8.dp else 2.dp,
                    top = 2.dp,
                    bottom = 2.dp,
                    end = 2.dp,
                ),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (count > 0) {
            Text(
                text = count.toString(),
                color = MaterialTheme.colorScheme.onSurface,
                overflow = TextOverflow.Ellipsis,
                style = FirefoxTheme.typography.caption,
                maxLines = 1,
            )
        }

        Icon(
            painter =
                if (isExpanded) {
                    painterResource(id = iconsR.drawable.mozac_ic_chevron_up_20)
                } else {
                    painterResource(id = iconsR.drawable.mozac_ic_chevron_down_20)
                },
            contentDescription = null,
            tint = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier =
                Modifier.semantics {
                    testTagsAsResourceId = true
                    testTag = EXTENSIONS_OPTION_CHEVRON
                },
        )
    }
}

@Composable
internal fun WebExtensionMenuItems(
    accessPoint: MenuAccessPoint,
    webExtensionMenuItems: Map<WebExtensionMenuItem, Addon?>,
    onWebExtensionMenuItemClick: () -> Unit,
    onWebExtensionMenuItemSettingsClick: (Addon) -> Unit,
    pendingExtensionUpdateIds: Set<String> = emptySet(),
) {
    Column(
        modifier =
            Modifier.padding(top = 2.dp).semantics {
                collectionInfo =
                    CollectionInfo(
                        rowCount = webExtensionMenuItems.size,
                        columnCount = 1,
                    )
            },
        verticalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        for (webExtensionMenuItem in webExtensionMenuItems) {
            val extension = webExtensionMenuItem.key
            val addon = webExtensionMenuItem.value

            WebExtensionMenuItem(
                label = extension.label,
                iconPainter =
                    extension.icon?.let { icon ->
                        BitmapPainter(image = icon.asImageBitmap())
                    } ?: painterResource(iconsR.drawable.mozac_ic_extension_fill_24),
                iconTint =
                    when (extension.icon) {
                        null -> MaterialTheme.colorScheme.onSurfaceVariant
                        else -> Color.Unspecified
                    },
                enabled = extension.enabled,
                badgeText = extension.badgeText,
                description =
                    stringResource(R.string.browser_menu_extension_review_update_description).takeIf {
                        extension.id in pendingExtensionUpdateIds
                    },
                onClick = {
                    if (accessPoint != MenuAccessPoint.External) {
                        onWebExtensionMenuItemClick()
                        extension.onClick()
                    } else {
                        // TODO(Bug 1959344): CustomTab should be visible to add-ons through the tabs API
                        // and it should be detected as active tab while in foreground.
                        Logger.error(
                            message = "WebExtensionsMenuItem does not does not support onClick in CustomTab mode",
                            throwable = NotImplementedError(),
                        )
                    }
                },
                onSettingsClick =
                    if (accessPoint != MenuAccessPoint.External) {
                        { addon?.let { onWebExtensionMenuItemSettingsClick(it) } }
                    } else {
                        null
                    },
            )
        }
    }
}

private fun previewWebExtensionMenuItems(): Map<WebExtensionMenuItem, Addon?> =
    mapOf(
        WebExtensionMenuItem(
            id = "",
            label = "Block some popups",
            enabled = true,
            icon = null,
            badgeText = "3",
            badgeTextColor = null,
            badgeBackgroundColor = null,
            onClick = {},
        ) to null,
        WebExtensionMenuItem(
            id = "",
            label = "Hello world",
            enabled = true,
            icon = null,
            badgeText = null,
            badgeTextColor = null,
            badgeBackgroundColor = null,
            onClick = {},
        ) to null,
    )

private data class ExtensionsMenuItemPreviewState(
    val isExtensionsProcessDisabled: Boolean,
    val isAllWebExtensionsDisabled: Boolean,
    val isExtensionsExpanded: Boolean,
    val webExtensionMenuCount: Int,
    val description: String,
)

private class ExtensionsMenuItemPreviewProvider :
    ThemedValueProvider<ExtensionsMenuItemPreviewState>(
        sequenceOf(
            ExtensionsMenuItemPreviewState(
                isExtensionsProcessDisabled = false,
                isAllWebExtensionsDisabled = false,
                isExtensionsExpanded = false,
                webExtensionMenuCount = 3,
                description = "3 extensions enabled",
            ),
            ExtensionsMenuItemPreviewState(
                isExtensionsProcessDisabled = false,
                isAllWebExtensionsDisabled = false,
                isExtensionsExpanded = true,
                webExtensionMenuCount = 2,
                description = "2 extensions enabled",
            ),
            ExtensionsMenuItemPreviewState(
                isExtensionsProcessDisabled = true,
                isAllWebExtensionsDisabled = false,
                isExtensionsExpanded = false,
                webExtensionMenuCount = 0,
                description = "Extensions are turned off",
            ),
        ),
        displayNames =
            listOf(
                "Collapsed",
                "Expanded with submenu",
                "Extensions process disabled",
            ),
    )

@Preview
@Composable
private fun ExtensionsMenuItemPreview(
    @PreviewParameter(ExtensionsMenuItemPreviewProvider::class) state: ThemedValue<ExtensionsMenuItemPreviewState>
) {
    FirefoxTheme(state.theme) {
        Surface {
            Column(modifier = Modifier.padding(all = FirefoxTheme.layout.space.static200)) {
                ExtensionsMenuItem(
                    inCustomTab = false,
                    isExtensionsProcessDisabled = state.value.isExtensionsProcessDisabled,
                    isAllWebExtensionsDisabled = state.value.isAllWebExtensionsDisabled,
                    isExtensionsExpanded = state.value.isExtensionsExpanded,
                    webExtensionMenuCount = state.value.webExtensionMenuCount,
                    extensionsMenuItemDescription = state.value.description,
                    onExtensionsMenuClick = {},
                    extensionSubmenu = {
                        if (state.value.isExtensionsExpanded) {
                            WebExtensionMenuItems(
                                accessPoint = MenuAccessPoint.Browser,
                                webExtensionMenuItems = previewWebExtensionMenuItems(),
                                onWebExtensionMenuItemClick = {},
                                onWebExtensionMenuItemSettingsClick = {},
                            )
                        }
                    },
                )
            }
        }
    }
}
