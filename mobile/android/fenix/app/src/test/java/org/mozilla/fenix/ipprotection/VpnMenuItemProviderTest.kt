/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection

import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.Role.Companion.Button
import androidx.compose.ui.semantics.Role.Companion.Switch
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.spyk
import io.mockk.verify
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItemActionButton
import mozilla.components.compose.menu.data.MenuItemBadge
import mozilla.components.compose.menu.data.MenuItemSummary
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.state.AccountState
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.Authorized
import mozilla.components.feature.ipprotection.store.state.DataVolume
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.feature.ipprotection.store.state.ProxyStatus
import mozilla.components.feature.ipprotection.store.state.Uninitialized
import mozilla.components.feature.ipprotection.store.state.UsageQuota
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.ui.icons.R as iconsR
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Vpn
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.fake.FakeMenuHost
import org.mozilla.fenix.components.menu.fake.reachableEvents
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.helpers.FenixGleanTestRule

@OptIn(ExperimentalAndroidComponentsApi::class)
@RunWith(AndroidJUnit4::class)
class VpnMenuItemProviderTest {
    @get:Rule val gleanRule = FenixGleanTestRule(testContext)

    private val menu = FakeMenuHost()

    @Test
    fun `GIVEN the user is not eligible WHEN building the menu item THEN don't show it`() = runTest {
        val provider = provider(IPProtectionStore(IPProtectionState()))

        assertTrue(provider.itemsFlow.value.isEmpty())
    }

    @Test
    fun `GIVEN IP protection is off WHEN building the menu item THEN show it as a switch that is off`() = runTest {
        val provider = provider(eligibleStore(Uninitialized))

        assertEquals(expectedItem(), provider.itemsFlow.value.single())
    }

    @Test
    fun `GIVEN IP protection is on WHEN building the menu item THEN show it as active`() = runTest {
        val provider = provider(eligibleStore(Authorized.Active))

        assertEquals(
            expectedItem(
                badgeTextRes = R.string.preferences_ip_protection_on,
                badgeState = MenuItemState.ACTIVE,
            ),
            provider.itemsFlow.value.single(),
        )
    }

    @Test
    fun `GIVEN authentication is needed WHEN building the menu item THEN show it as a button rather than a switch`() =
        runTest {
            val store =
                IPProtectionStore(
                    IPProtectionState(
                        eligibilityStatus = EligibilityStatus.Eligible,
                        serviceStatus = ServiceState.Unauthenticated,
                        accountState = AccountState(AccountStatus.NoAccount),
                    )
                )
            val provider = provider(store)

            assertEquals(
                expectedItem(badgeTextRes = R.string.ip_protection_menu_try_vpn_cta, role = Button),
                provider.itemsFlow.value.single(),
            )
        }

    @Test
    fun `GIVEN the data limit was reached WHEN building the menu item THEN explain it in the summary`() = runTest {
        val store =
            IPProtectionStore(
                IPProtectionState(
                    eligibilityStatus = EligibilityStatus.Eligible,
                    serviceStatus = ServiceState.Ready,
                    proxyStatus = Authorized.DataLimitReached,
                    usageQuota =
                        UsageQuota.Metered(
                            max = DataVolume.roundedGigabytes(5),
                            remaining = DataVolume.Zero,
                        ),
                )
            )
        val provider = provider(store)

        assertEquals(
            MenuItemSummary(
                text = Text.Resource(R.string.ip_protection_menu_limit_reached, listOf(5L)),
                state = MenuItemState.WARNING,
            ),
            (provider.itemsFlow.value.single() as StandardMenuItem).summary,
        )
    }

    @Test
    fun `GIVEN the data limit is unknown WHEN building the menu item THEN don't show a summary`() = runTest {
        val provider = provider(eligibleStore(Uninitialized))

        assertNull((provider.itemsFlow.value.single() as StandardMenuItem).summary)
    }

    @Test
    fun `GIVEN target is CustomizeMenu WHEN building the menu item THEN show it without click events`() = runTest {
        val provider =
            VpnMenuItemProvider(
                eligibleStore(Uninitialized),
                target = MenuTarget.CustomizeMenu,
                scope = backgroundScope,
            )

        assertEquals(
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.ip_protection_toggle_label)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_globe_24),
                onClickEvent = null,
                state = MenuItemState.DEFAULT,
            ),
            provider.itemsFlow.value.single(),
        )
    }

    @OptIn(ExperimentalCoroutinesApi::class)
    @Test
    fun `WHEN the user stops being eligible THEN stop showing the menu item`() = runTest {
        val store = eligibleStore(Uninitialized)
        val provider = provider(store)

        assertEquals(expectedItem(), provider.itemsFlow.value.single())

        store.dispatch(IPProtectionAction.EligibilityAction.EligibilityChanged(EligibilityStatus.Ineligible))
        runCurrent()

        assertTrue(provider.itemsFlow.value.isEmpty())
    }

    @Test
    fun `GIVEN IP protection is off WHEN toggling it THEN turn it on and keep the menu open`() = runTest {
        val store = spyk(eligibleStore(Authorized.Idle))

        provider(store).onEvent(MenuAction.IPProtectionToggle, menu)

        verify { store.dispatch(IPProtectionAction.ActivationAction.Toggle) }
        assertFalse(menu.isUsed)
        assertNotNull(Vpn.menuTurnedOn.testGetValue())
    }

    @Test
    fun `GIVEN IP protection is on WHEN toggling it THEN turn it off and keep the menu open`() = runTest {
        val store = spyk(eligibleStore(Authorized.Active))

        provider(store).onEvent(MenuAction.IPProtectionToggle, menu)

        verify { store.dispatch(IPProtectionAction.ActivationAction.Toggle) }
        assertFalse(menu.isUsed)
        assertNotNull(Vpn.menuTurnedOff.testGetValue())
    }

    @Test
    fun `GIVEN signing in is needed WHEN toggling IP protection THEN show its settings instead`() = runTest {
        val store =
            IPProtectionStore(
                IPProtectionState(
                    eligibilityStatus = EligibilityStatus.Eligible,
                    serviceStatus = ServiceState.Unauthenticated,
                    accountState = AccountState(AccountStatus.NoAccount),
                )
            )

        provider(store).onEvent(MenuAction.IPProtectionToggle, menu)

        assertEquals(settingsDirections, menu.directions)
        assertNotNull(Vpn.menuTryItTapped.testGetValue())
    }

    @Test
    fun `GIVEN the service has not accepted the signed-in account yet WHEN toggling IP protection THEN show its settings without recording a try-it tap`() =
        runTest {
            val store =
                IPProtectionStore(
                    IPProtectionState(
                        eligibilityStatus = EligibilityStatus.Eligible,
                        serviceStatus = ServiceState.Unauthenticated,
                        accountState = AccountState(AccountStatus.Authenticated),
                    )
                )

            provider(store).onEvent(MenuAction.IPProtectionToggle, menu)

            assertEquals(settingsDirections, menu.directions)
            assertNull(Vpn.menuTryItTapped.testGetValue())
        }

    @Test
    fun `GIVEN IP protection has not reported its state yet WHEN toggling it THEN nothing happens`() = runTest {
        val store = spyk(IPProtectionStore(IPProtectionState(eligibilityStatus = EligibilityStatus.Eligible)))

        provider(store).onEvent(MenuAction.IPProtectionToggle, menu)

        verify(exactly = 0) { store.dispatch(IPProtectionAction.ActivationAction.Toggle) }
        assertFalse(menu.isUsed)
        assertNull(Vpn.menuTurnedOn.testGetValue())
    }

    @Test
    fun `WHEN asking for the IP protection settings THEN show them in place of the menu`() = runTest {
        provider(eligibleStore(Authorized.Idle)).onEvent(MenuAction.Navigate.IPProtectionSettings, menu)

        assertEquals(settingsDirections, menu.directions)
        assertEquals("Menu", Vpn.settingsPageTapped.testGetValue()?.last()?.extra?.get("entrypoint"))
    }

    @Test
    fun `WHEN IP protection is on or off THEN handle all events the item can dispatch and no others`() = runTest {
        listOf(Authorized.Idle, Authorized.Active).forEach { status ->
            val provider = provider(eligibleStore(status))

            val events = provider.itemsFlow.value.single().reachableEvents()

            assertTrue(events.all { provider.handles(it) }, "Not all of $events are handled")
            assertFalse(provider.handles(MenuAction.Navigate.Settings))
        }
    }

    @Test
    fun `GIVEN the data limit was reached WHEN building the menu item THEN the item is disabled`() = runTest {
        val provider = provider(eligibleStore(Authorized.DataLimitReached))

        assertEquals(
            expectedItem(
                badgeTextRes = R.string.ip_protection_menu_paused,
                badgeState = MenuItemState.DISABLED,
                state = MenuItemState.DISABLED,
            ),
            provider.itemsFlow.value.single(),
        )
    }

    @Test
    fun `GIVEN the data limit was reached WHEN the menu item is toggled THEN do nothing`() = runTest {
        val store = spyk(eligibleStore(Authorized.DataLimitReached))

        provider(store).onEvent(MenuAction.IPProtectionToggle, menu)

        verify(exactly = 0) { store.dispatch(IPProtectionAction.ActivationAction.Toggle) }
        assertNull(Vpn.menuTurnedOn.testGetValue())
        assertNull(Vpn.menuTurnedOff.testGetValue())
    }

    private val settingsDirections =
        NavGraphDirections.actionGlobalIpProtectionFragment(entrypoint = FenixFxAEntryPoint.IPProtectionMainMenu)

    // The item is kept up to date on a scope that runTest cancels at the end of each test.
    private fun TestScope.provider(
        ipProtectionStore: IPProtectionStore,
        target: MenuTarget = MenuTarget.BrowserTab,
    ) = VpnMenuItemProvider(ipProtectionStore = ipProtectionStore, target = target, scope = backgroundScope)

    private fun eligibleStore(proxyStatus: ProxyStatus) =
        IPProtectionStore(
            IPProtectionState(
                eligibilityStatus = EligibilityStatus.Eligible,
                serviceStatus = ServiceState.Ready,
                proxyStatus = proxyStatus,
            )
        )

    private fun expectedItem(
        badgeTextRes: Int = R.string.preferences_ip_protection_off,
        badgeState: MenuItemState = MenuItemState.DEFAULT,
        role: Role = Switch,
        state: MenuItemState = MenuItemState.DEFAULT,
    ) =
        StandardMenuItem(
            title = MenuItemTitle(Text.Resource(R.string.ip_protection_toggle_label)),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_globe_24),
            role = role,
            state = state,
            onClickEvent = MenuAction.IPProtectionToggle,
            badge = MenuItemBadge(text = Text.Resource(badgeTextRes), state = badgeState),
            actionButton =
                MenuItemActionButton(
                    icon = iconsR.drawable.mozac_ic_chevron_right_24,
                    contentDescription = Text.Resource(R.string.ip_protection_navigate_settings),
                    onClickEvent = MenuAction.Navigate.IPProtectionSettings,
                    showDivider = true,
                ),
        )
}
