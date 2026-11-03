/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.compose

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.VerticalDivider
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import kotlin.math.roundToLong
import mozilla.components.compose.base.modifier.debouncedClickable
import mozilla.components.compose.base.modifier.thenConditional
import mozilla.components.compose.base.theme.ThemedValue
import mozilla.components.compose.base.theme.ThemedValueProvider
import mozilla.components.feature.ipprotection.store.state.DataVolume
import mozilla.components.feature.ipprotection.store.state.IPProtectionStatus
import mozilla.components.feature.ipprotection.store.state.gigabytes
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.store.IPProtectionMenuState
import org.mozilla.fenix.compose.list.IconListItem
import org.mozilla.fenix.theme.FirefoxTheme

private val MENU_ITEM_MIN_HEIGHT = 52.dp

/**
 * A menu item showing the current IP Protection status.
 *
 * State is driven externally via [IPProtectionMenuState] so that the caller (menu fragment) can subscribe to the IP
 * protection feature store and dispatch updates.
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
    val statusDescription = badgeText(state.status)
    val role =
        if (state.status == IPProtectionStatus.AuthRequired || state.status == IPProtectionStatus.AwaitingService) {
            Role.Button
        } else {
            Role.Switch
        }
    val isEnabled = state.status != IPProtectionStatus.DataLimitReached

    IconListItem(
        label = stringResource(R.string.ip_protection_toggle_label),
        modifier =
            Modifier.wrapContentSize()
                .clip(MaterialTheme.shapes.extraSmall)
                .background(MaterialTheme.colorScheme.surfaceBright)
                .thenConditional(Modifier.debouncedClickable(role = role) { onToggle() }) { isEnabled }
                .semantics(mergeDescendants = true) {
                    stateDescription = statusDescription
                    if (!isEnabled) {
                        disabled()
                        this.role = role
                    }
                },
        enabled = isEnabled,
        colors = ListItemDefaults.colors(supportingColor = MaterialTheme.colorScheme.error),
        description = descriptionText(state),
        minHeight = MENU_ITEM_MIN_HEIGHT,
        beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_globe_24),
        contentPaddingListItem = PaddingValues(start = FirefoxTheme.layout.space.dynamic200),
    ) {
        Row(
            modifier = Modifier.fillMaxHeight(),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Badge(
                badgeText = statusDescription,
                modifier = Modifier.clearAndSetSemantics {},
                state = badgeState(state.status),
            )

            Spacer(Modifier.width(FirefoxTheme.layout.space.dynamic200))
            VerticalDivider(
                modifier = Modifier.fillMaxHeight().padding(vertical = FirefoxTheme.layout.space.static100),
                color = MaterialTheme.colorScheme.outlineVariant,
            )

            Box(
                modifier =
                    Modifier.fillMaxHeight()
                        .clickable(role = Role.Button, onClick = onNavigate)
                        .padding(horizontal = 12.dp),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_chevron_right_24),
                    contentDescription = stringResource(R.string.ip_protection_navigate_settings),
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
private fun descriptionText(state: IPProtectionMenuState): String? =
    when (state.status) {
        IPProtectionStatus.DataLimitReached ->
            if (state.dataLimit != null) {
                stringResource(R.string.ip_protection_menu_limit_reached, state.dataLimit.gigabytes.roundToLong())
            } else {
                null
            }

        IPProtectionStatus.Unavailable,
        IPProtectionStatus.Uninitialized,
        IPProtectionStatus.AuthRequired,
        IPProtectionStatus.AwaitingService,
        IPProtectionStatus.Disabled,
        IPProtectionStatus.Activating,
        IPProtectionStatus.Enabled,
        IPProtectionStatus.ConnectionError -> null
    }

@Composable
private fun badgeText(status: IPProtectionStatus): String =
    when (status) {
        IPProtectionStatus.Unavailable,
        IPProtectionStatus.Uninitialized,
        IPProtectionStatus.Disabled -> stringResource(R.string.preferences_ip_protection_off)
        IPProtectionStatus.Enabled -> stringResource(R.string.preferences_ip_protection_on)
        IPProtectionStatus.AwaitingService,
        IPProtectionStatus.Activating -> stringResource(R.string.ip_protection_menu_connecting)
        IPProtectionStatus.DataLimitReached -> stringResource(R.string.ip_protection_menu_paused)
        IPProtectionStatus.ConnectionError -> stringResource(R.string.ip_protection_menu_error)
        IPProtectionStatus.AuthRequired -> stringResource(R.string.ip_protection_menu_try_vpn_cta)
    }

private fun badgeState(status: IPProtectionStatus): MenuItemState =
    when (status) {
        IPProtectionStatus.Enabled,
        IPProtectionStatus.AwaitingService,
        IPProtectionStatus.Activating -> MenuItemState.ACTIVE
        IPProtectionStatus.ConnectionError -> MenuItemState.WARNING
        IPProtectionStatus.DataLimitReached -> MenuItemState.DISABLED
        IPProtectionStatus.Unavailable,
        IPProtectionStatus.Uninitialized,
        IPProtectionStatus.Disabled,
        IPProtectionStatus.AuthRequired -> MenuItemState.ENABLED
    }

private val FiftyGb = DataVolume.roundedGigabytes(50)

private class IPProtectionMenuStatePreviewProvider :
    ThemedValueProvider<IPProtectionMenuState>(
        baseValues =
            IPProtectionStatus.entries
                .map { status ->
                    IPProtectionMenuState(
                        status = status,
                        dataLimit = if (status == IPProtectionStatus.DataLimitReached) FiftyGb else null,
                    )
                }
                .asSequence(),
        getDisplayName = { _, value -> value.status.name },
    )

@Preview
@Composable
private fun IPProtectionMenuItemPreview(
    @PreviewParameter(IPProtectionMenuStatePreviewProvider::class) state: ThemedValue<IPProtectionMenuState>
) {
    FirefoxTheme(theme = state.theme) {
        MenuGroup {
            IPProtectionMenuItem(
                state = state.value,
                onToggle = {},
                onNavigate = {},
            )
        }
    }
}
