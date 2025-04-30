/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.ui

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.Card
import androidx.compose.material.Icon
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.Divider
import mozilla.components.compose.base.annotation.LightDarkPreview
import mozilla.components.compose.base.menu.DropdownMenu
import mozilla.components.compose.base.menu.MenuItem.CheckableItem
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.text.value
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.compose.MenuGroup
import org.mozilla.fenix.components.menu.compose.MenuItem
import org.mozilla.fenix.components.menu.compose.MenuScaffold
import org.mozilla.fenix.components.menu.compose.MenuTextItem
import org.mozilla.fenix.compose.LinkText
import org.mozilla.fenix.compose.LinkTextState
import org.mozilla.fenix.compose.SwitchWithLabel
import org.mozilla.fenix.settings.PhoneFeature
import org.mozilla.fenix.settings.trustpanel.store.AutoplayValue
import org.mozilla.fenix.settings.trustpanel.store.WebsiteInfoState
import org.mozilla.fenix.settings.trustpanel.store.WebsitePermission
import org.mozilla.fenix.theme.FirefoxTheme

private val ROUNDED_CORNER_SHAPE = RoundedCornerShape(4.dp)

@Suppress("LongParameterList", "LongMethod")
@Composable
internal fun ProtectionPanel(
    icon: Bitmap?,
    isTrackingProtectionEnabled: Boolean,
    numberOfTrackersBlocked: Int,
    websiteInfoState: WebsiteInfoState,
    websitePermissions: List<WebsitePermission>,
    onTrackerBlockedMenuClick: () -> Unit,
    onTrackingProtectionToggleClick: () -> Unit,
    onClearSiteDataMenuClick: () -> Unit,
    onConnectionSecurityClick: () -> Unit,
    onPrivacySecuritySettingsClick: () -> Unit,
    onAutoplayValueClick: (AutoplayValue) -> Unit,
    onToggleablePermissionClick: (WebsitePermission.Toggleable) -> Unit,
) {
    MenuScaffold(
        header = {
            ProtectionPanelHeader(
                icon = icon,
                websiteInfoState = websiteInfoState,
                onConnectionSecurityClick = onConnectionSecurityClick,
            )
        },
    ) {
        MenuGroup {
            ProtectionPanelBanner(
                isSecured = websiteInfoState.isSecured,
                isTrackingProtectionEnabled = isTrackingProtectionEnabled,
            )

            if (numberOfTrackersBlocked == 0) {
                MenuItem(
                    label = stringResource(id = R.string.protection_panel_no_trackers_blocked),
                    beforeIconPainter = painterResource(id = R.drawable.mozac_ic_shield_24),
                )
            } else {
                MenuItem(
                    label = stringResource(
                        id = R.string.protection_panel_num_trackers_blocked,
                        numberOfTrackersBlocked,
                    ),
                    beforeIconPainter = painterResource(id = R.drawable.mozac_ic_shield_24),
                    onClick = onTrackerBlockedMenuClick,
                    afterIconPainter = painterResource(id = R.drawable.mozac_ic_chevron_right_24),
                )
            }

            Divider(color = FirefoxTheme.colors.borderSecondary)

            SwitchWithLabel(
                label = stringResource(id = R.string.protection_panel_etp_toggle_label),
                checked = isTrackingProtectionEnabled,
                modifier = Modifier.padding(start = 16.dp, top = 6.dp, end = 9.dp, bottom = 14.dp),
                description = if (isTrackingProtectionEnabled) {
                    stringResource(id = R.string.protection_panel_etp_toggle_enabled_description)
                } else {
                    stringResource(id = R.string.protection_panel_etp_toggle_disabled_description)
                },
                onCheckedChange = { onTrackingProtectionToggleClick() },
            )
        }

        if (websitePermissions.isNotEmpty()) {
            WebsitePermissionsMenuGroup(
                websitePermissions = websitePermissions,
                onAutoplayValueClick = onAutoplayValueClick,
                onToggleablePermissionClick = onToggleablePermissionClick,
            )
        }

        MenuGroup {
            MenuTextItem(
                label = stringResource(id = R.string.clear_site_data),
                onClick = onClearSiteDataMenuClick,
            )
        }

        LinkText(
            text = stringResource(id = R.string.protection_panel_privacy_and_security_settings),
            linkTextStates = listOf(
                LinkTextState(
                    text = stringResource(id = R.string.protection_panel_privacy_and_security_settings),
                    url = "",
                    onClick = { onPrivacySecuritySettingsClick() },
                ),
            ),
            linkTextColor = FirefoxTheme.colors.textAccent,
            linkTextDecoration = TextDecoration.Underline,
        )
    }
}

@Composable
private fun ProtectionPanelBanner(
    isSecured: Boolean,
    isTrackingProtectionEnabled: Boolean,
) {
    val backgroundColor: Color
    val imageId: Int
    val title: String
    val description: String

    if (!isSecured) {
        backgroundColor = FirefoxTheme.colors.layerCritical
        imageId = R.drawable.protection_panel_not_secure
        title = stringResource(id = R.string.protection_panel_banner_not_secure_title)
        description = stringResource(id = R.string.protection_panel_banner_not_secure_description)
    } else if (!isTrackingProtectionEnabled) {
        backgroundColor = FirefoxTheme.colors.layer3
        imageId = R.drawable.protection_panel_not_protected
        title = stringResource(id = R.string.protection_panel_banner_not_protected_title)
        description = stringResource(
            id = R.string.protection_panel_banner_not_protected_description,
            stringResource(id = R.string.app_name),
        )
    } else {
        backgroundColor = FirefoxTheme.colors.layerAccentNonOpaque
        imageId = R.drawable.protection_panel_protected
        title = stringResource(
            id = R.string.protection_panel_banner_protected_title,
            stringResource(id = R.string.app_name),
        )
        description = stringResource(id = R.string.protection_panel_banner_protected_description)
    }

    Card(
        modifier = Modifier
            .padding(start = 8.dp, top = 8.dp, end = 8.dp)
            .fillMaxWidth(),
        backgroundColor = backgroundColor,
        elevation = 0.dp,
        shape = ROUNDED_CORNER_SHAPE,
    ) {
        Row(
            modifier = Modifier.padding(start = 12.dp, top = 16.dp, end = 16.dp, bottom = 16.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Image(
                modifier = Modifier.size(90.dp),
                painter = painterResource(id = imageId),
                contentDescription = null,
            )

            Column(
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Text(
                    text = title,
                    color = FirefoxTheme.colors.textPrimary,
                    style = FirefoxTheme.typography.headline7,
                )

                Text(
                    text = description,
                    color = FirefoxTheme.colors.textPrimary,
                    style = FirefoxTheme.typography.body2,
                )
            }
        }
    }
}

@Composable
private fun WebsitePermissionsMenuGroup(
    websitePermissions: List<WebsitePermission>,
    onAutoplayValueClick: (AutoplayValue) -> Unit,
    onToggleablePermissionClick: (WebsitePermission.Toggleable) -> Unit,
) {
    MenuGroup {
        Row(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
        ) {
            Text(
                text = stringResource(id = R.string.protection_panel_permissions_title),
                color = FirefoxTheme.colors.textAccent,
                style = FirefoxTheme.typography.headline8,
            )
        }

        websitePermissions.forEachIndexed { index, websitePermission ->
            if (index != 0) {
                Divider(color = FirefoxTheme.colors.borderSecondary)
            }

            MenuItem(
                label = stringResource(id = websitePermission.deviceFeature.getLabelId()),
                beforeIconPainter = painterResource(id = websitePermission.deviceFeature.getIconId()),
                afterContent = when (websitePermission) {
                    is WebsitePermission.Autoplay -> {
                        { AutoplayDropdownMenu(websitePermission, onAutoplayValueClick) }
                    }
                    is WebsitePermission.Toggleable -> {
                        { WebsitePermissionToggle(websitePermission, onToggleablePermissionClick) }
                    }
                },
            )
        }
    }
}

@Composable
private fun WebsitePermissionToggle(
    websitePermission: WebsitePermission.Toggleable,
    onToggleablePermissionClick: (WebsitePermission.Toggleable) -> Unit,
) {
    val toggleLabel = if (websitePermission.isBlockedByAndroid) {
        stringResource(id = R.string.phone_feature_blocked_by_android)
    } else if (websitePermission.isEnabled) {
        stringResource(id = R.string.preference_option_phone_feature_allowed)
    } else {
        stringResource(id = R.string.preference_option_phone_feature_blocked)
    }

    Column(
        modifier = Modifier
            .clickable { onToggleablePermissionClick(websitePermission) }
            .semantics { role = Role.Switch },
    ) {
        Text(
            text = toggleLabel,
            color = FirefoxTheme.colors.textAccent,
            style = FirefoxTheme.typography.body1,
        )
    }
}

@Composable
private fun AutoplayDropdownMenu(
    websitePermission: WebsitePermission.Autoplay,
    onAutoplayValueClick: (AutoplayValue) -> Unit,
) {
    val density = LocalDensity.current
    var expanded by remember { mutableStateOf(false) }
    var contextMenuWidthDp by remember { mutableStateOf(0.dp) }

    val dropdownItems = AutoplayValue.entries.map { autoplayValueEntry ->
        CheckableItem(
            text = Text.String(stringResource(id = autoplayValueEntry.title)),
            isChecked = autoplayValueEntry == websitePermission.autoplayValue,
            onClick = { onAutoplayValueClick(autoplayValueEntry) },
        )
    }

    Column(
        modifier = Modifier
            .clickable { expanded = true }
            .semantics { role = Role.DropdownList },
    ) {
        val placeholderText = dropdownItems.find { it.isChecked }?.text?.value ?: ""

        Row(
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = placeholderText,
                color = FirefoxTheme.colors.textAccent,
                style = FirefoxTheme.typography.body1,
            )

            Spacer(modifier = Modifier.width(4.dp))

            Box {
                Icon(
                    painter = painterResource(id = R.drawable.mozac_ic_dropdown_arrow),
                    contentDescription = null,
                    tint = FirefoxTheme.colors.iconAccentViolet,
                )

                if (expanded) {
                    DropdownMenu(
                        expanded = true,
                        menuItems = dropdownItems,
                        modifier = Modifier
                            .onGloballyPositioned { coordinates ->
                                contextMenuWidthDp = with(density) {
                                    coordinates.size.width.toDp()
                                }
                            },
                        onDismissRequest = { expanded = false },
                    )
                }
            }
        }
    }
}

@LightDarkPreview
@Composable
private fun ProtectionPanelPreview() {
    FirefoxTheme {
        Column(
            modifier = Modifier
                .background(color = FirefoxTheme.colors.layer3),
        ) {
            ProtectionPanel(
                icon = null,
                websiteInfoState = WebsiteInfoState(
                    isSecured = true,
                    websiteUrl = "https://www.mozilla.org",
                    websiteTitle = "Mozilla",
                    certificateName = "",
                ),
                isTrackingProtectionEnabled = true,
                numberOfTrackersBlocked = 5,
                websitePermissions = listOf(
                    WebsitePermission.Autoplay(
                        AutoplayValue.AUTOPLAY_BLOCK_AUDIBLE,
                        true,
                        PhoneFeature.AUTOPLAY,
                    ),
                ),
                onTrackerBlockedMenuClick = {},
                onTrackingProtectionToggleClick = {},
                onClearSiteDataMenuClick = {},
                onConnectionSecurityClick = {},
                onPrivacySecuritySettingsClick = {},
                onAutoplayValueClick = {},
                onToggleablePermissionClick = {},
            )
        }
    }
}
