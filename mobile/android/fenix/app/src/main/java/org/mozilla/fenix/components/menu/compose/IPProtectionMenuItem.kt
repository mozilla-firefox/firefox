/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.compose

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.VerticalDivider
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.theme.surfaceDimVariant
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.store.IPProtectionMenuState
import org.mozilla.fenix.components.menu.store.IPProtectionMenuStatus
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.PreviewThemeProvider
import org.mozilla.fenix.theme.Theme
import mozilla.components.ui.icons.R as iconsR

private val MENU_ITEM_MIN_HEIGHT = 52.dp
private val ROUNDED_CORNER = RoundedCornerShape(4.dp)

/**
 * A menu item showing the current IP Protection status.
 *
 * State is driven externally via [IPProtectionMenuState] so that the caller (menu fragment)
 * can subscribe to the IP protection feature store and dispatch updates.
 *
 * @param state The current [IPProtectionMenuState] to display.
 * @param onToggle Called when the label row is tapped to toggle IP Protection on or off.
 * @param onNavigate Called when the chevron is tapped to open the IP Protection settings screen.
 */
@Composable
internal fun IPProtectionMenuItem(
    state: IPProtectionMenuState,
    onToggle: () -> Unit,
    onNavigate: () -> Unit,
) {
    Row(
        modifier = Modifier
            .wrapContentSize()
            .clip(ROUNDED_CORNER)
            .background(MaterialTheme.colorScheme.surfaceDimVariant)
            .height(IntrinsicSize.Min)
            .defaultMinSize(minHeight = MENU_ITEM_MIN_HEIGHT),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(
            modifier = Modifier
                .weight(1f)
                .fillMaxHeight()
                .clickable(role = Role.Button) { onToggle() }
                .padding(horizontal = FirefoxTheme.layout.space.dynamic200),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static200),
        ) {
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_globe_24),
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurface,
            )

            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = stringResource(R.string.ip_protection_toggle_label),
                    color = MaterialTheme.colorScheme.onSurface,
                    style = FirefoxTheme.typography.subtitle1,
                    overflow = TextOverflow.Ellipsis,
                    maxLines = 1,
                )

                if (state.status == IPProtectionMenuStatus.DataLimitReached) {
                    Text(
                        text = stringResource(R.string.ip_protection_menu_limit_reached, state.dataLimitGb),
                        color = MaterialTheme.colorScheme.error,
                        style = FirefoxTheme.typography.caption,
                        overflow = TextOverflow.Ellipsis,
                        maxLines = 1,
                    )
                }
            }

            Badge(
                badgeText = badgeText(state.status),
                state = badgeState(state.status),
            )
        }

        VerticalDivider(
            modifier = Modifier
                .fillMaxHeight()
                .padding(vertical = FirefoxTheme.layout.space.static100)
                .width(1.dp),
            color = MaterialTheme.colorScheme.outlineVariant,
        )

        Box(
            modifier = Modifier
                .fillMaxHeight()
                .clickable(role = Role.Button, onClick = onNavigate)
                .padding(horizontal = 12.dp),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_chevron_right_24),
                contentDescription = stringResource(R.string.ip_protection_navigate_settings),
                tint = MaterialTheme.colorScheme.onSurface,
            )
        }
    }
}

@Composable
private fun badgeText(status: IPProtectionMenuStatus): String = when (status) {
    IPProtectionMenuStatus.Disabled -> stringResource(R.string.preferences_ip_protection_off)
    IPProtectionMenuStatus.Enabled -> stringResource(R.string.preferences_ip_protection_on)
    IPProtectionMenuStatus.Activating -> stringResource(R.string.ip_protection_menu_connecting)
    IPProtectionMenuStatus.DataLimitReached -> stringResource(R.string.ip_protection_menu_paused)
    IPProtectionMenuStatus.ConnectionError -> stringResource(R.string.ip_protection_menu_error)
    IPProtectionMenuStatus.AuthRequired -> stringResource(R.string.ip_protection_menu_auth_required)
}

private fun badgeState(status: IPProtectionMenuStatus): MenuItemState = when (status) {
    IPProtectionMenuStatus.Enabled -> MenuItemState.ACTIVE
    IPProtectionMenuStatus.ConnectionError -> MenuItemState.WARNING
    IPProtectionMenuStatus.DataLimitReached -> MenuItemState.DISABLED
    IPProtectionMenuStatus.Disabled,
    IPProtectionMenuStatus.Activating,
    IPProtectionMenuStatus.AuthRequired,
    -> MenuItemState.ENABLED
}

@FlexibleWindowLightDarkPreview
@Composable
private fun IPProtectionMenuItemOffPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme = theme) {
        MenuGroup {
            IPProtectionMenuItem(
                state = IPProtectionMenuState(status = IPProtectionMenuStatus.Disabled),
                onToggle = {},
                onNavigate = {},
            )
        }
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun IPProtectionMenuItemOnPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme = theme) {
        MenuGroup {
            IPProtectionMenuItem(
                state = IPProtectionMenuState(status = IPProtectionMenuStatus.Enabled),
                onToggle = {},
                onNavigate = {},
            )
        }
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun IPProtectionMenuItemConnectingPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme = theme) {
        MenuGroup {
            IPProtectionMenuItem(
                state = IPProtectionMenuState(status = IPProtectionMenuStatus.Activating),
                onToggle = {},
                onNavigate = {},
            )
        }
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun IPProtectionMenuItemPausedPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme = theme) {
        MenuGroup {
            IPProtectionMenuItem(
                state = IPProtectionMenuState(
                    status = IPProtectionMenuStatus.DataLimitReached,
                    dataLimitGb = 50,
                ),
                onToggle = {},
                onNavigate = {},
            )
        }
    }
}
