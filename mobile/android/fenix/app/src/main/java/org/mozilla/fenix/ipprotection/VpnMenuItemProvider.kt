/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection

import androidx.compose.ui.semantics.Role
import kotlin.math.roundToLong
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemActionButton
import mozilla.components.compose.menu.data.MenuItemBadge
import mozilla.components.compose.menu.data.MenuItemSummary
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.feature.ipprotection.store.state.IPProtectionStatus
import mozilla.components.feature.ipprotection.store.state.gigabytes
import mozilla.components.feature.ipprotection.store.state.ipProtectionStatus
import mozilla.components.feature.ipprotection.store.state.isEligible
import mozilla.components.ui.icons.R as iconsR
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.Vpn
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.IPProtectionMenuState
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.components.menu.toMenuState

/**
 * [MenuItemProvider] for the menu item allowing to toggle IP protection, or to open its settings.
 *
 * Shown only to users eligible for the feature, and kept in sync with the connection status so that the menu can stay
 * open while connecting.
 *
 * @param ipProtectionStore [IPProtectionStore] with information about the current VPN functionality, used also to
 *   toggle it.
 * @param target [MenuTarget] for which this menu item would be shown for.
 * @param scope [CoroutineScope] used to keep the item up to date for as long as it can be shown.
 */
class VpnMenuItemProvider(
    private val ipProtectionStore: IPProtectionStore,
    private val target: MenuTarget,
    scope: CoroutineScope,
) : MenuItemProvider {
    override val itemsFlow: StateFlow<List<MenuItem>> =
        ipProtectionStore.stateFlow
            .map { listOfNotNull(it.ipProtectionMenuItem()) }
            .stateIn(
                scope = scope,
                started = SharingStarted.Eagerly,
                initialValue = listOfNotNull(ipProtectionStore.state.ipProtectionMenuItem()),
            )

    override fun handles(event: MenuEvent) =
        event == MenuAction.IPProtectionToggle || event == MenuAction.Navigate.IPProtectionSettings

    /** The menu is deliberately left open while connecting, so that the user can see the status change. */
    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        when (event) {
            MenuAction.IPProtectionToggle -> toggle(menu)
            MenuAction.Navigate.IPProtectionSettings -> {
                Vpn.settingsPageTapped.record(Vpn.SettingsPageTappedExtra(entrypoint = "Menu"))
                menu.navigate(settingsDirections())
            }
            else -> Unit
        }
    }

    private fun toggle(menu: MenuHost) {
        when (ipProtectionStore.state.ipProtectionStatus) {
            IPProtectionStatus.Disabled -> {
                Vpn.menuTurnedOn.record()
                ipProtectionStore.dispatch(IPProtectionAction.ActivationAction.Toggle)
            }

            IPProtectionStatus.Enabled -> {
                Vpn.menuTurnedOff.record()
                ipProtectionStore.dispatch(IPProtectionAction.ActivationAction.Toggle)
            }

            IPProtectionStatus.AuthRequired -> {
                Vpn.menuTryItTapped.record(NoExtras())
                menu.navigate(settingsDirections())
            }

            IPProtectionStatus.AwaitingService -> menu.navigate(settingsDirections())

            IPProtectionStatus.Activating,
            IPProtectionStatus.ConnectionError -> ipProtectionStore.dispatch(IPProtectionAction.ActivationAction.Toggle)

            IPProtectionStatus.DataLimitReached,
            IPProtectionStatus.Unavailable,
            IPProtectionStatus.Uninitialized -> Unit
        }
    }

    private fun settingsDirections() =
        NavGraphDirections.actionGlobalIpProtectionFragment(entrypoint = FenixFxAEntryPoint.IPProtectionMainMenu)

    /** IP protection is only offered to the users that are eligible for it. */
    private fun IPProtectionState.ipProtectionMenuItem() =
        when (isEligible) {
            true ->
                when (target) {
                    MenuTarget.CustomizeMenu -> DEFAULT_MENU_ITEM
                    else -> toMenuState().toMenuItem()
                }

            else -> null
        }

    private fun IPProtectionMenuState.toMenuItem() =
        DEFAULT_MENU_ITEM.copy(
            // Only a toggle while it can actually be toggled - otherwise tapping it opens the VPN settings instead.
            role =
                when (status) {
                    IPProtectionStatus.AuthRequired,
                    IPProtectionStatus.AwaitingService -> Role.Button
                    else -> Role.Switch
                },
            onClickEvent = MenuAction.IPProtectionToggle,
            summary = dataLimitSummary,
            badge = MenuItemBadge(text = Text.Resource(status.badgeTextRes), state = status.badgeState),
            actionButton =
                MenuItemActionButton(
                    icon = iconsR.drawable.mozac_ic_chevron_right_24,
                    contentDescription = Text.Resource(R.string.ip_protection_navigate_settings),
                    onClickEvent = MenuAction.Navigate.IPProtectionSettings,
                    showDivider = true,
                ),
            state =
                when (status) {
                    IPProtectionStatus.DataLimitReached -> MenuItemState.DISABLED
                    else -> MenuItemState.DEFAULT
                },
        )

    companion object {
        private val DEFAULT_MENU_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.ip_protection_toggle_label)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_globe_24),
            )
    }
}

private val IPProtectionMenuState.dataLimitSummary: MenuItemSummary?
    get() =
        when {
            status == IPProtectionStatus.DataLimitReached && dataLimit != null ->
                MenuItemSummary(
                    text =
                        Text.Resource(
                            R.string.ip_protection_menu_limit_reached,
                            listOf(dataLimit.gigabytes.roundToLong()),
                        ),
                    state = MenuItemState.WARNING,
                )
            else -> null
        }

private val IPProtectionStatus.badgeTextRes: Int
    get() =
        when (this) {
            IPProtectionStatus.Unavailable,
            IPProtectionStatus.Uninitialized,
            IPProtectionStatus.Disabled -> R.string.preferences_ip_protection_off
            IPProtectionStatus.Enabled -> R.string.preferences_ip_protection_on
            IPProtectionStatus.AwaitingService,
            IPProtectionStatus.Activating -> R.string.ip_protection_menu_connecting
            IPProtectionStatus.DataLimitReached -> R.string.ip_protection_menu_paused
            IPProtectionStatus.ConnectionError -> R.string.ip_protection_menu_error
            IPProtectionStatus.AuthRequired -> R.string.ip_protection_menu_try_vpn_cta
        }

private val IPProtectionStatus.badgeState: MenuItemState
    get() =
        when (this) {
            IPProtectionStatus.Enabled,
            IPProtectionStatus.AwaitingService,
            IPProtectionStatus.Activating -> MenuItemState.ACTIVE
            IPProtectionStatus.ConnectionError -> MenuItemState.WARNING
            IPProtectionStatus.DataLimitReached -> MenuItemState.DISABLED
            IPProtectionStatus.Unavailable,
            IPProtectionStatus.Uninitialized,
            IPProtectionStatus.Disabled,
            IPProtectionStatus.AuthRequired -> MenuItemState.DEFAULT
        }
