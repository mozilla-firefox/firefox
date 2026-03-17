/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.compose

import androidx.compose.foundation.LocalIndication
import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CollectionInfo
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.collectionInfo
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.base.theme.surfaceDimVariant
import mozilla.components.feature.addons.Addon
import mozilla.components.feature.addons.ui.displayName
import mozilla.components.feature.addons.ui.summary
import mozilla.components.service.fxa.manager.AccountState
import mozilla.components.service.fxa.manager.AccountState.AuthenticationProblem
import mozilla.components.service.fxa.store.Account
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.menu.MenuDialogTestTag.DESKTOP_SITE_OFF
import org.mozilla.fenix.components.menu.MenuDialogTestTag.DESKTOP_SITE_ON
import org.mozilla.fenix.components.menu.MenuDialogTestTag.MORE_OPTION_CHEVRON
import org.mozilla.fenix.components.menu.compose.header.MozillaAccountMenuItem
import org.mozilla.fenix.components.menu.store.WebExtensionMenuItem
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.PreviewThemeProvider
import org.mozilla.fenix.theme.Theme
import org.mozilla.fenix.theme.ThemedValue
import org.mozilla.fenix.theme.ThemedValueProvider
import mozilla.components.ui.icons.R as iconsR

/**
 * Wrapper column containing the main menu items.
 *
 * @param accessPoint The [MenuAccessPoint] that was used to navigate to the menu dialog.
 * @param account [Account] information available for a synced account.
 * @param accountState The [AccountState] of a Mozilla account.
 * @param showQuitMenu Whether or not the button to delete browsing data and quit
 * should be visible.
 * @param isBottomToolbar Whether or not the browser toolbar is at the bottom.
 * @param isExpandedToolbarEnabled Whether or not the expanded toolbar layout is enabled.
 * @param isSiteLoading Whether or not the tab is loading.
 * @param isExtensionsExpanded Whether or not the extensions menu is expanded.
 * @param isMoreMenuExpanded Whether or not the more menu is expanded.
 * @param isBookmarked Whether or not the current tab is bookmarked.
 * @param isDesktopMode Whether or not the desktop mode is enabled.
 * @param isPdf Whether or not the current tab is a PDF.
 * @param isPrivate Whether or not the current browsing mode is private
 * @param isReaderViewActive Whether or not Reader View is active or not.
 * @param isExtensionsProcessDisabled Whether or not the extensions process is disabled due to extension errors.
 * @param isMoreMenuHighlighted Whether or not the more menu icon is highlighted.
 * @param isAllWebExtensionsDisabled Whether or not all web extensions are disabled.
 * @param canGoBack Whether or not the back button is enabled.
 * @param canGoForward Whether or not the forward button is enabled.
 * @param scrollState The [ScrollState] used for vertical scrolling.
 * @param showBanner Whether or not the default browser banner should be shown.
 * @param isDownloadHighlighted `true` if the downloads menu item should be visually highlighted.
 * @param webExtensionMenuCount The number of web extensions.
 * @param onMoreMenuClick Invoked when the user clicks on the more menu item.
 * @param onCustomizeReaderViewMenuClick Invoked when the user clicks on the Customize Reader View button.
 * @param onMozillaAccountButtonClick Invoked when the user clicks on Mozilla account button.
 * @param onSettingsButtonClick Invoked when the user clicks on the settings button.
 * @param onBookmarkPageMenuClick Invoked when the user clicks on the bookmark page menu item.
 * @param onEditBookmarkButtonClick Invoked when the user clicks on the edit bookmark button.
 * @param onSwitchToDesktopSiteMenuClick Invoked when the user clicks on the switch to desktop site
 * menu toggle.
 * @param onFindInPageMenuClick Invoked when the user clicks on the find in page menu item.
 * @param onBannerClick Invoked when the user clicks on the banner.
 * @param onBannerDismiss Invoked when the user clicks on the dismiss button.
 * @param onExtensionsMenuClick Invoked when the user clicks on the extensions menu item.
 * @param onBookmarksMenuClick Invoked when the user clicks on the bookmarks menu item.
 * @param onHistoryMenuClick Invoked when the user clicks on the history menu item.
 * @param onDownloadsMenuClick Invoked when the user clicks on the downloads menu item.
 * @param onPasswordsMenuClick Invoked when the user clicks on the passwords menu item.
 * @param onQuitMenuClick Invoked when the user clicks on the quit menu item.
 * @param onBackButtonClick Invoked when the user clicks on the back button.
 * @param onForwardButtonClick Invoked when the user clicks on the forward button.
 * @param onRefreshButtonClick Invoked when the user clicks on the refresh button.
 * @param onStopButtonClick Invoked when the user clicks on the stop button.
 * @param onShareButtonClick Invoked when the user clicks on the share button.
 * @param extensionsMenuItemDescription The label of extensions menu item description.
 * @param moreSettingsSubmenu The content of more menu item.
 * @param extensionSubmenu The content of extensions menu item to avoid configuration during animation.
 */
@Suppress("LongParameterList", "LongMethod", "CyclomaticComplexMethod", "CognitiveComplexMethod")
@Composable
fun MainMenu(
    accessPoint: MenuAccessPoint,
    account: Account?,
    accountState: AccountState,
    showQuitMenu: Boolean,
    isBottomToolbar: Boolean,
    isExpandedToolbarEnabled: Boolean,
    isSiteLoading: Boolean,
    isExtensionsExpanded: Boolean,
    isMoreMenuExpanded: Boolean,
    isBookmarked: Boolean,
    isDesktopMode: Boolean,
    isPdf: Boolean,
    isPrivate: Boolean,
    isReaderViewActive: Boolean,
    isExtensionsProcessDisabled: Boolean,
    isMoreMenuHighlighted: Boolean,
    isAllWebExtensionsDisabled: Boolean,
    canGoBack: Boolean,
    canGoForward: Boolean,
    scrollState: ScrollState,
    showBanner: Boolean,
    isDownloadHighlighted: Boolean,
    webExtensionMenuCount: Int,
    onMoreMenuClick: () -> Unit,
    onCustomizeReaderViewMenuClick: () -> Unit,
    onMozillaAccountButtonClick: () -> Unit,
    onSettingsButtonClick: () -> Unit,
    onBookmarkPageMenuClick: () -> Unit,
    onEditBookmarkButtonClick: () -> Unit,
    onSwitchToDesktopSiteMenuClick: () -> Unit,
    onFindInPageMenuClick: () -> Unit,
    onBannerClick: () -> Unit,
    onBannerDismiss: () -> Unit,
    onExtensionsMenuClick: () -> Unit,
    onBookmarksMenuClick: () -> Unit,
    onHistoryMenuClick: () -> Unit,
    onDownloadsMenuClick: () -> Unit,
    onPasswordsMenuClick: () -> Unit,
    onQuitMenuClick: () -> Unit,
    onBackButtonClick: (longPress: Boolean) -> Unit,
    onForwardButtonClick: (longPress: Boolean) -> Unit,
    onRefreshButtonClick: (longPress: Boolean) -> Unit,
    onStopButtonClick: () -> Unit,
    onShareButtonClick: () -> Unit,
    extensionsMenuItemDescription: String?,
    moreSettingsSubmenu: @Composable () -> Unit,
    extensionSubmenu: @Composable () -> Unit,
) {
    MenuFrame(
        modifier = Modifier.background(AcornTheme.colors.layer3),
        contentModifier = Modifier
            .padding(
                start = 8.dp,
                top = if (accessPoint != MenuAccessPoint.Home &&
                    (isBottomToolbar || isExpandedToolbarEnabled)
                ) {
                    0.dp
                } else {
                    8.dp
                },
                end = 8.dp,
                bottom = if (accessPoint != MenuAccessPoint.Home &&
                    (isBottomToolbar || isExpandedToolbarEnabled)
                ) {
                    84.dp
                } else {
                    16.dp
                },
            ),
        scrollState = scrollState,
        header = {
            if (accessPoint != MenuAccessPoint.Home && !isBottomToolbar && !isExpandedToolbarEnabled) {
                MenuNavigation(
                    isSiteLoading = isSiteLoading,
                    isExtensionsExpanded = isExtensionsExpanded,
                    isMoreMenuExpanded = isMoreMenuExpanded,
                    onBackButtonClick = onBackButtonClick,
                    onForwardButtonClick = onForwardButtonClick,
                    onRefreshButtonClick = onRefreshButtonClick,
                    onStopButtonClick = onStopButtonClick,
                    onShareButtonClick = onShareButtonClick,
                    goBackState = if (canGoBack) MenuItemState.ENABLED else MenuItemState.DISABLED,
                    goForwardState = if (canGoForward) MenuItemState.ENABLED else MenuItemState.DISABLED,
                )

                if (scrollState.canScrollBackward) {
                    HorizontalDivider()
                }
            }
        },
        footer = {
            if (accessPoint != MenuAccessPoint.Home && (isBottomToolbar || isExpandedToolbarEnabled)) {
                if (scrollState.canScrollBackward) {
                    HorizontalDivider()
                }

                MenuNavigation(
                    isSiteLoading = isSiteLoading,
                    isExtensionsExpanded = isExtensionsExpanded,
                    isMoreMenuExpanded = isMoreMenuExpanded,
                    onBackButtonClick = onBackButtonClick,
                    onForwardButtonClick = onForwardButtonClick,
                    onRefreshButtonClick = onRefreshButtonClick,
                    onStopButtonClick = onStopButtonClick,
                    onShareButtonClick = onShareButtonClick,
                    goBackState = if (canGoBack) MenuItemState.ENABLED else MenuItemState.DISABLED,
                    goForwardState = if (canGoForward) MenuItemState.ENABLED else MenuItemState.DISABLED,
                )
            }
        },
    ) {
        if (isReaderViewActive && accessPoint != MenuAccessPoint.Home) {
            MenuGroup {
                MenuItem(
                    label = stringResource(id = R.string.browser_menu_customize_reader_view_2),
                    beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_tool_24),
                    onClick = onCustomizeReaderViewMenuClick,
                )
            }
        }

        if (accessPoint == MenuAccessPoint.Home && showBanner) {
            MenuBanner(
                onDismiss = {
                    onBannerDismiss()
                },
                onClick = {
                    onBannerClick()
                },
            )
        }

        if (accessPoint == MenuAccessPoint.Home) {
            MenuGroup {
                ExtensionsMenuItem(
                    inCustomTab = false,
                    isPrivate = isPrivate,
                    isExtensionsProcessDisabled = isExtensionsProcessDisabled,
                    isExtensionsExpanded = isExtensionsExpanded,
                    isAllWebExtensionsDisabled = isAllWebExtensionsDisabled,
                    webExtensionMenuCount = webExtensionMenuCount,
                    extensionsMenuItemDescription = extensionsMenuItemDescription,
                    onExtensionsMenuClick = onExtensionsMenuClick,
                    extensionSubmenu = extensionSubmenu,
                )
            }
        }

        if (accessPoint == MenuAccessPoint.Browser) {
            ToolsAndActionsMenuGroup(
                isBookmarked = isBookmarked,
                isDesktopMode = isDesktopMode,
                isPdf = isPdf,
                isPrivate = isPrivate,
                isExtensionsProcessDisabled = isExtensionsProcessDisabled,
                isExtensionsExpanded = isExtensionsExpanded,
                isMoreMenuHighlighted = isMoreMenuHighlighted,
                moreMenuExpanded = isMoreMenuExpanded,
                webExtensionMenuCount = webExtensionMenuCount,
                isAllWebExtensionsDisabled = isAllWebExtensionsDisabled,
                onExtensionsMenuClick = onExtensionsMenuClick,
                onBookmarkPageMenuClick = onBookmarkPageMenuClick,
                onEditBookmarkButtonClick = onEditBookmarkButtonClick,
                onSwitchToDesktopSiteMenuClick = onSwitchToDesktopSiteMenuClick,
                onFindInPageMenuClick = onFindInPageMenuClick,
                onMoreMenuClick = onMoreMenuClick,
                extensionsMenuItemDescription = extensionsMenuItemDescription,
                moreSettingsSubmenu = moreSettingsSubmenu,
                extensionSubmenu = extensionSubmenu,
            )
        }

        LibraryMenuGroup(
            isDownloadHighlighted = isDownloadHighlighted,
            onBookmarksMenuClick = onBookmarksMenuClick,
            onHistoryMenuClick = onHistoryMenuClick,
            onDownloadsMenuClick = onDownloadsMenuClick,
            onPasswordsMenuClick = onPasswordsMenuClick,
        )

        MenuGroup {
            MozillaAccountMenuItem(
                account = account,
                accountState = accountState,
                isPrivate = isPrivate,
                onClick = onMozillaAccountButtonClick,
            )

            MenuItem(
                label = stringResource(id = R.string.browser_menu_settings),
                beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_settings_24),
                onClick = onSettingsButtonClick,
            )
        }

        if (showQuitMenu) {
            QuitMenuGroup(
                onQuitMenuClick = onQuitMenuClick,
            )
        }
    }
}

@Composable
private fun QuitMenuGroup(
    onQuitMenuClick: () -> Unit,
) {
    MenuGroup {
        MenuItem(
            label = stringResource(
                id = R.string.browser_menu_delete_browsing_data_on_quit,
                stringResource(id = R.string.app_name),
            ),
            beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_cross_circle_fill_24),
            state = MenuItemState.WARNING,
            onClick = onQuitMenuClick,
        )
    }
}

@Suppress("LongParameterList", "LongMethod", "CognitiveComplexMethod")
@Composable
private fun ToolsAndActionsMenuGroup(
    isBookmarked: Boolean,
    isDesktopMode: Boolean,
    isPdf: Boolean,
    isPrivate: Boolean,
    isExtensionsProcessDisabled: Boolean,
    isExtensionsExpanded: Boolean,
    isMoreMenuHighlighted: Boolean,
    moreMenuExpanded: Boolean,
    webExtensionMenuCount: Int,
    isAllWebExtensionsDisabled: Boolean,
    onExtensionsMenuClick: () -> Unit,
    onBookmarkPageMenuClick: () -> Unit,
    onEditBookmarkButtonClick: () -> Unit,
    onSwitchToDesktopSiteMenuClick: () -> Unit,
    onFindInPageMenuClick: () -> Unit,
    onMoreMenuClick: () -> Unit,
    extensionsMenuItemDescription: String?,
    moreSettingsSubmenu: @Composable () -> Unit,
    extensionSubmenu: @Composable () -> Unit,
) {
    MenuGroup {
        val labelId = R.string.browser_menu_desktop_site
        val badgeText: String
        val menuItemState: MenuItemState
        val badgeBackgroundColor: Color

        if (isDesktopMode) {
            badgeText = stringResource(id = R.string.browser_feature_desktop_site_on)
            badgeBackgroundColor = MaterialTheme.colorScheme.primaryContainer
            menuItemState = if (isPdf) MenuItemState.DISABLED else MenuItemState.ACTIVE
        } else {
            badgeText = stringResource(id = R.string.browser_feature_desktop_site_off)
            badgeBackgroundColor = MaterialTheme.colorScheme.surfaceContainerHighest
            menuItemState = if (isPdf) MenuItemState.DISABLED else MenuItemState.ENABLED
        }

        if (isBookmarked) {
            MenuItem(
                label = stringResource(id = R.string.browser_menu_edit_bookmark),
                beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_bookmark_fill_24),
                state = MenuItemState.ACTIVE,
                onClick = onEditBookmarkButtonClick,
            )
        } else {
            MenuItem(
                label = stringResource(id = R.string.browser_menu_bookmark_this_page_2),
                beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_bookmark_24),
                onClick = onBookmarkPageMenuClick,
            )
        }

        MenuItem(
            label = stringResource(id = R.string.browser_menu_find_in_page),
            beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_search_24),
            onClick = onFindInPageMenuClick,
        )

        MenuItem(
            modifier = Modifier.semantics {
                testTagsAsResourceId = true
                testTag = when (menuItemState) {
                    MenuItemState.ACTIVE -> DESKTOP_SITE_ON
                    else -> DESKTOP_SITE_OFF
                }
            },
            label = stringResource(id = labelId),
            stateDescription = badgeText,
            beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_device_desktop_24),
            state = menuItemState,
            onClick = onSwitchToDesktopSiteMenuClick,
        ) {
            if (menuItemState == MenuItemState.DISABLED) {
                return@MenuItem
            }

            Badge(
                badgeText = badgeText,
                state = menuItemState,
            )
        }

        ExtensionsMenuItem(
            inCustomTab = false,
            isPrivate = isPrivate,
            isExtensionsProcessDisabled = isExtensionsProcessDisabled,
            isExtensionsExpanded = isExtensionsExpanded,
            isAllWebExtensionsDisabled = isAllWebExtensionsDisabled,
            webExtensionMenuCount = webExtensionMenuCount,
            extensionsMenuItemDescription = extensionsMenuItemDescription,
            onExtensionsMenuClick = onExtensionsMenuClick,
            extensionSubmenu = extensionSubmenu,
        )

        if (!moreMenuExpanded) {
            MoreMenuButtonGroup(
                isMoreMenuHighlighted = isMoreMenuHighlighted,
                onMoreMenuClick = onMoreMenuClick,
            )
        }

        ExpandableMenuItemAnimation(
            isExpanded = moreMenuExpanded,
            content = moreSettingsSubmenu,
        )
    }
}

@Composable
private fun MoreMenuButtonGroup(
    isMoreMenuHighlighted: Boolean,
    onMoreMenuClick: () -> Unit,
) {
    MenuItem(
        label = stringResource(id = R.string.browser_menu_more_settings),
        stateDescription = "Collapsed",
        beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_ellipsis_horizontal_24),
        isBeforeIconHighlighted = isMoreMenuHighlighted,
        onClick = onMoreMenuClick,
    ) {
        Row(
            modifier = Modifier
                .background(
                    color = MaterialTheme.colorScheme.surfaceContainerHighest,
                    shape = RoundedCornerShape(16.dp),
                )
                .padding(2.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(
                painter = painterResource(id = iconsR.drawable.mozac_ic_chevron_down_20),
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurface,
                modifier = Modifier.semantics {
                    testTagsAsResourceId = true
                    testTag = MORE_OPTION_CHEVRON
                },
            )
        }
    }
}

@Composable
@Suppress("LongMethod")
private fun LibraryMenuGroup(
    isDownloadHighlighted: Boolean = false,
    onBookmarksMenuClick: () -> Unit,
    onHistoryMenuClick: () -> Unit,
    onDownloadsMenuClick: () -> Unit,
    onPasswordsMenuClick: () -> Unit,
) {
    val spacerWidth = 2.dp
    val innerRounding = 4.dp
    val outerRounding = 28.dp

    val leftShape = RoundedCornerShape(
        topStart = outerRounding,
        topEnd = innerRounding,
        bottomStart = outerRounding,
        bottomEnd = innerRounding,
    )
    val middleShape = RoundedCornerShape(innerRounding)
    val rightShape = RoundedCornerShape(
        topStart = innerRounding,
        topEnd = outerRounding,
        bottomStart = innerRounding,
        bottomEnd = outerRounding,
    )

    Row(
        Modifier
            .fillMaxWidth()
            .height(IntrinsicSize.Min)
            .semantics {
                this.collectionInfo = CollectionInfo(
                    rowCount = 1,
                    columnCount = 4,
                )
            },
        horizontalArrangement = Arrangement.SpaceEvenly,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        LibraryMenuItem(
            modifier = Modifier
                .weight(1f)
                .fillMaxHeight(),
            iconRes = iconsR.drawable.mozac_ic_history_24,
            labelRes = R.string.library_history,
            shape = leftShape,
            index = 0,
            onClick = onHistoryMenuClick,
        )

        Spacer(Modifier.width(spacerWidth))

        LibraryMenuItem(
            modifier = Modifier
                .weight(1f)
                .fillMaxHeight(),
            iconRes = iconsR.drawable.mozac_ic_bookmark_tray_fill_24,
            labelRes = R.string.library_bookmarks,
            shape = middleShape,
            index = 1,
            onClick = onBookmarksMenuClick,
        )

        Spacer(Modifier.width(spacerWidth))

        LibraryMenuItem(
            modifier = Modifier
                .weight(1f)
                .fillMaxHeight(),
            isHighlighted = isDownloadHighlighted,
            iconRes = iconsR.drawable.mozac_ic_download_24,
            labelRes = R.string.library_downloads,
            shape = middleShape,
            index = 2,
            onClick = onDownloadsMenuClick,
        )

        Spacer(Modifier.width(spacerWidth))

        LibraryMenuItem(
            modifier = Modifier
                .weight(1f)
                .fillMaxHeight(),
            iconRes = iconsR.drawable.mozac_ic_login_24,
            labelRes = R.string.browser_menu_passwords,
            shape = rightShape,
            index = 3,
            onClick = onPasswordsMenuClick,
        )
    }
}

@Suppress("LongParameterList")
@Composable
internal fun Addons(
    accessPoint: MenuAccessPoint,
    availableAddons: List<Addon>,
    webExtensionMenuItems: Map<WebExtensionMenuItem, Addon?>,
    addonInstallationInProgress: Addon?,
    recommendedAddons: List<Addon>,
    onAddonSettingsClick: (Addon) -> Unit,
    onAddonClick: (Addon) -> Unit,
    onInstallAddonClick: (Addon) -> Unit,
    onManageExtensionsMenuClick: () -> Unit,
    onDiscoverMoreExtensionsMenuClick: () -> Unit,
    onWebExtensionMenuItemClick: () -> Unit,
) {
    Column(
        verticalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        if (accessPoint == MenuAccessPoint.Home && availableAddons.isNotEmpty()) {
            AddonsMenuItems(
                availableAddons = availableAddons,
                iconPainter = painterResource(id = iconsR.drawable.mozac_ic_settings_24),
                onClick = {
                    onWebExtensionMenuItemClick()
                    onAddonClick(it)
                },
                onIconClick = { onAddonSettingsClick(it) },
            )
        } else if (accessPoint == MenuAccessPoint.Browser && webExtensionMenuItems.isNotEmpty()) {
            WebExtensionMenuItems(
                accessPoint = accessPoint,
                webExtensionMenuItems = webExtensionMenuItems,
                onWebExtensionMenuItemClick = onWebExtensionMenuItemClick,
                onWebExtensionMenuItemSettingsClick = onAddonSettingsClick,
            )
        } else if (recommendedAddons.isNotEmpty()) {
            AddonsMenuItems(
                availableAddons = recommendedAddons,
                addonInstallationInProgress = addonInstallationInProgress,
                onClick = {
                    onWebExtensionMenuItemClick()
                    onAddonClick(it)
                },
                onIconClick = { onInstallAddonClick(it) },
            )
        }

        if (availableAddons.isNotEmpty()) {
            MoreExtensionsMenuItem(
                onManageExtensionsMenuClick,
                stringResource(id = R.string.browser_menu_manage_extensions),
            )
        } else {
            MoreExtensionsMenuItem(
                onDiscoverMoreExtensionsMenuClick,
                stringResource(id = R.string.browser_menu_discover_more_extensions),
            )
        }
    }
}

@Composable
private fun AddonsMenuItems(
    availableAddons: List<Addon>,
    addonInstallationInProgress: Addon? = null,
    iconPainter: Painter? = null,
    onClick: (Addon) -> Unit,
    onIconClick: (Addon) -> Unit,
) {
    Column(
        modifier = Modifier.padding(top = 2.dp)
            .semantics {
                collectionInfo = CollectionInfo(
                    rowCount = availableAddons.size,
                    columnCount = 1,
                )
            },
        verticalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        for ((index, addon) in availableAddons.withIndex()) {
            val description = stringResource(
                R.string.browser_menu_extension_plus_icon_content_description_2,
                addon.displayName(LocalContext.current),
            )

            AddonMenuItem(
                addon = addon,
                addonInstallationInProgress = addonInstallationInProgress,
                iconPainter = iconPainter ?: painterResource(id = iconsR.drawable.mozac_ic_plus_24),
                iconDescription = if (iconPainter != null) addon.summary(LocalContext.current) else description,
                showDivider = true,
                index = index,
                onClick = { onClick(addon) },
                onIconClick = { onIconClick(addon) },
            )
        }
    }
}

@Composable
private fun MoreExtensionsMenuItem(
    onClick: () -> Unit,
    label: String,
) {
    Column(
        modifier = Modifier
            .clickable(
                interactionSource = remember { MutableInteractionSource() },
                indication = LocalIndication.current,
            ) { onClick.invoke() }
            .clearAndSetSemantics {
                role = Role.Button
                contentDescription = label
            }
            .wrapContentSize()
            .clip(shape = RoundedCornerShape(4.dp))
            .background(
                color = MaterialTheme.colorScheme.surfaceDimVariant,
            ),
    ) {
        MenuTextItem(
            label = label,
            iconPainter = painterResource(id = iconsR.drawable.mozac_ic_external_link_24),
            modifier = Modifier.padding(start = 40.dp),
        )
    }
}

@Preview
@Composable
private fun MenuDialogPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        Column(
            modifier = Modifier
                .background(color = MaterialTheme.colorScheme.surface),
        ) {
            MainMenu(
                accessPoint = MenuAccessPoint.Browser,
                account = null,
                accountState = AuthenticationProblem,
                isPrivate = false,
                showQuitMenu = true,
                isBottomToolbar = false,
                isExpandedToolbarEnabled = false,
                isSiteLoading = false,
                isExtensionsExpanded = false,
                isMoreMenuExpanded = true,
                isBookmarked = false,
                isDesktopMode = false,
                isPdf = false,
                isReaderViewActive = false,
                isExtensionsProcessDisabled = true,
                isMoreMenuHighlighted = false,
                isAllWebExtensionsDisabled = false,
                canGoBack = true,
                canGoForward = true,
                extensionsMenuItemDescription = "No extensions enabled",
                scrollState = ScrollState(0),
                showBanner = true,
                isDownloadHighlighted = true,
                webExtensionMenuCount = 1,
                onMoreMenuClick = {},
                onCustomizeReaderViewMenuClick = {},
                onMozillaAccountButtonClick = {},
                onSettingsButtonClick = {},
                onBookmarkPageMenuClick = {},
                onEditBookmarkButtonClick = {},
                onSwitchToDesktopSiteMenuClick = {},
                onFindInPageMenuClick = {},
                onBannerClick = {},
                onBannerDismiss = {},
                onExtensionsMenuClick = {},
                onBookmarksMenuClick = {},
                onHistoryMenuClick = {},
                onDownloadsMenuClick = {},
                onPasswordsMenuClick = {},
                onQuitMenuClick = {},
                onBackButtonClick = {},
                onForwardButtonClick = {},
                onRefreshButtonClick = {},
                onStopButtonClick = {},
                onShareButtonClick = {},
                moreSettingsSubmenu = {},
                extensionSubmenu = {},
            )
        }
    }
}

@Suppress("LongMethod")
@Preview
@Composable
private fun MenuDialogPrivatePreview(
    @PreviewParameter(SiteLoadingPreviewParameterProvider::class) state: ThemedValue<Boolean>,
) {
    FirefoxTheme(theme = state.theme) {
        Column(
            modifier = Modifier
                .background(color = MaterialTheme.colorScheme.surface),
        ) {
            MainMenu(
                accessPoint = MenuAccessPoint.Home,
                account = null,
                accountState = AuthenticationProblem,
                isPrivate = false,
                showQuitMenu = true,
                isBottomToolbar = true,
                isExpandedToolbarEnabled = false,
                isSiteLoading = state.value,
                isExtensionsExpanded = true,
                isMoreMenuExpanded = true,
                isBookmarked = false,
                isDesktopMode = false,
                isPdf = false,
                isReaderViewActive = false,
                isExtensionsProcessDisabled = false,
                isMoreMenuHighlighted = false,
                canGoBack = true,
                canGoForward = true,
                isAllWebExtensionsDisabled = false,
                extensionsMenuItemDescription = "No extensions enabled",
                scrollState = ScrollState(0),
                showBanner = true,
                isDownloadHighlighted = true,
                webExtensionMenuCount = 0,
                onMoreMenuClick = {},
                onCustomizeReaderViewMenuClick = {},
                onMozillaAccountButtonClick = {},
                onSettingsButtonClick = {},
                onBookmarkPageMenuClick = {},
                onEditBookmarkButtonClick = {},
                onSwitchToDesktopSiteMenuClick = {},
                onFindInPageMenuClick = {},
                onBannerClick = {},
                onBannerDismiss = {},
                onExtensionsMenuClick = {},
                onBookmarksMenuClick = {},
                onHistoryMenuClick = {},
                onDownloadsMenuClick = {},
                onPasswordsMenuClick = {},
                onQuitMenuClick = {},
                onBackButtonClick = {},
                onForwardButtonClick = {},
                onRefreshButtonClick = {},
                onStopButtonClick = {},
                onShareButtonClick = {},
                moreSettingsSubmenu = {},
                extensionSubmenu = {
                    Addons(
                        accessPoint = MenuAccessPoint.Home,
                        availableAddons = listOf(),
                        webExtensionMenuItems = mapOf(),
                        addonInstallationInProgress = null,
                        recommendedAddons = listOf(
                            Addon(
                                id = "id",
                                translatableName = mapOf(Addon.DEFAULT_LOCALE to "name"),
                                translatableSummary = mapOf(Addon.DEFAULT_LOCALE to "summary"),
                            ),
                            Addon(
                                id = "id",
                                translatableName = mapOf(Addon.DEFAULT_LOCALE to "name"),
                                translatableSummary = mapOf(Addon.DEFAULT_LOCALE to "summary"),
                            ),
                        ),
                        onAddonSettingsClick = {},
                        onAddonClick = {},
                        onInstallAddonClick = {},
                        onManageExtensionsMenuClick = {},
                        onDiscoverMoreExtensionsMenuClick = {},
                        onWebExtensionMenuItemClick = {},
                    )
                },
            )
        }
    }
}

/**
 * A [PreviewParameterProvider] implementation that provides boolean values
 * representing the loading state of a site.
 */
class SiteLoadingPreviewParameterProvider : ThemedValueProvider<Boolean>(
    sequenceOf(true, false),
)
