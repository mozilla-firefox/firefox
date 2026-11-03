/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.accounts

import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import java.io.ByteArrayInputStream
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertTrue
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItemSummary
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.ui.MenuItemIconBitmap
import mozilla.components.compose.menu.ui.MenuItemIconDrawable
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.concept.fetch.Client
import mozilla.components.concept.fetch.MutableHeaders
import mozilla.components.concept.fetch.Response
import mozilla.components.concept.sync.Avatar
import mozilla.components.service.fxa.manager.AccountState
import mozilla.components.service.fxa.manager.AccountState.Authenticated
import mozilla.components.service.fxa.manager.AccountState.AuthenticationProblem
import mozilla.components.service.fxa.manager.AccountState.NotAuthenticated
import mozilla.components.service.fxa.manager.AccountState.Unknown
import mozilla.components.service.fxa.store.Account
import mozilla.components.service.fxa.store.SyncState
import mozilla.components.service.fxa.store.SyncStatus
import mozilla.components.service.fxa.store.SyncStore
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.ui.icons.R as iconsR
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.fake.FakeMenuHost
import org.mozilla.fenix.components.menu.store.MenuAction
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class MozillaAccountMenuItemProviderTest {
    private val syncState = MutableStateFlow(SyncState())
    private val syncStore: SyncStore = mockk {
        every { stateFlow } returns syncState
        every { state } answers { syncState.value }
    }

    @Test
    fun `GIVEN an account is not signed in WHEN providing the item THEN offer signing in`() = runTest {
        syncState.value = SyncState(accountState = NotAuthenticated)

        assertEquals(
            expectedItem(
                title = Text.Resource(R.string.browser_menu_sign_in),
                summary = Text.Resource(R.string.browser_menu_sign_in_caption_3),
                accountState = NotAuthenticated,
            ),
            provider().itemsFlow.value.single(),
        )
    }

    @Test
    fun `GIVEN target is CustomizeMenu WHEN providing the item THEN show item without click event`() = runTest {
        syncState.value = SyncState(accountState = NotAuthenticated)

        assertEquals(
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_sign_in)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_avatar_circle_24),
                onClickEvent = null,
                state = MenuItemState.DEFAULT,
            ),
            provider(target = MenuTarget.CustomizeMenu).itemsFlow.value.single(),
        )
    }

    @Test
    fun `GIVEN the account is not known yet WHEN providing the item THEN offer signing in`() = runTest {
        syncState.value = SyncState(accountState = Unknown)

        assertEquals(
            expectedItem(
                title = Text.Resource(R.string.browser_menu_sign_in),
                summary = Text.Resource(R.string.browser_menu_sign_in_caption_3),
                accountState = Unknown,
            ),
            provider().itemsFlow.value.single(),
        )
    }

    @Test
    fun `GIVEN someone is signed in WHEN providing the item THEN show who they are`() = runTest {
        syncState.value = SyncState(account = account, accountState = Authenticated)

        assertEquals(
            expectedItem(
                title = Text.String(DISPLAY_NAME),
                summary = Text.Resource(R.string.browser_menu_signed_in_caption),
                accountState = Authenticated,
            ),
            provider().itemsFlow.value.single(),
        )
    }

    @Test
    fun `GIVEN the signed in account has no display name WHEN providing the item THEN show its email`() = runTest {
        syncState.value = SyncState(account = account.copy(displayName = null), accountState = Authenticated)

        assertEquals(
            expectedItem(
                title = Text.String(EMAIL),
                summary = Text.Resource(R.string.browser_menu_signed_in_caption),
                accountState = Authenticated,
            ),
            provider().itemsFlow.value.single(),
        )
    }

    @Test
    fun `GIVEN the signed in account has no details WHEN providing the item THEN offer its settings`() = runTest {
        syncState.value =
            SyncState(account = account.copy(displayName = null, email = null), accountState = Authenticated)

        assertEquals(
            expectedItem(
                title = Text.Resource(R.string.browser_menu_account_settings),
                summary = Text.Resource(R.string.browser_menu_signed_in_caption),
                accountState = Authenticated,
            ),
            provider().itemsFlow.value.single(),
        )
    }

    @Test
    fun `GIVEN the account needs signing in again WHEN providing the item THEN with informing that syncing is paused`() =
        runTest {
            syncState.value = SyncState(account = account, accountState = AuthenticationProblem)

            val item = provider().itemsFlow.value.single() as StandardMenuItem

            assertEquals(Text.String(DISPLAY_NAME), item.title.text)
            assertEquals(
                MenuItemSummary(
                    text = Text.Resource(R.string.browser_menu_syncing_paused_caption),
                    state = MenuItemState.WARNING,
                    maxLines = SUMMARY_MAX_LINES,
                ),
                item.summary,
            )
            // The warning icon needs to not be tinted and so needs to be different from MenuItemIconRes.
            assertIs<MenuItemIconDrawable>(item.icon)
        }

    @Test
    fun `GIVEN the account needing signing in again has no details WHEN providing the item THEN inform about just needing to sign back in`() =
        runTest {
            syncState.value =
                SyncState(
                    account = account.copy(displayName = null, email = null),
                    accountState = AuthenticationProblem,
                )

            val item = provider().itemsFlow.value.single() as StandardMenuItem

            assertEquals(Text.Resource(R.string.browser_menu_sign_back_in_to_sync), item.title.text)
        }

    @Test
    fun `GIVEN the signed in account has an avatar WHEN it is loaded THEN show it`() = runTest {
        syncState.value =
            SyncState(
                account = account.copy(avatar = Avatar(url = AVATAR_URL, isDefault = false)),
                accountState = Authenticated,
            )
        val provider = provider(httpClient = clientReturning(avatarBytes))

        // The item is shown with a placeholder icon until the avatar is downloaded.
        assertIs<MenuItemIconRes>((provider.itemsFlow.value.single() as StandardMenuItem).icon)

        val menuItemWithAvatar =
            provider.itemsFlow.first { (it.single() as StandardMenuItem).icon is MenuItemIconBitmap }.single()

        val avatar = ((menuItemWithAvatar as StandardMenuItem).icon as MenuItemIconBitmap).icon
        assertEquals(avatar.width, avatar.height)
    }

    @Test
    fun `GIVEN the account needs signing in again WHEN it has an avatar THEN keep warning about it instead`() =
        runTest {
            syncState.value =
                SyncState(
                    account = account.copy(avatar = Avatar(url = AVATAR_URL, isDefault = false)),
                    accountState = AuthenticationProblem,
                )
            val httpClient = clientReturning(avatarBytes)

            val item = provider(httpClient = httpClient).itemsFlow.value.single() as StandardMenuItem

            assertIs<MenuItemIconDrawable>(item.icon)
            verify(exactly = 0) { httpClient.fetch(any()) }
        }

    @Test
    fun `WHEN the account changes THEN update the item`() = runTest {
        syncState.value = SyncState(accountState = NotAuthenticated)
        val provider = provider()
        assertEquals(Text.Resource(R.string.browser_menu_sign_in), provider.itemsFlow.value.single().title.text)

        syncState.value = SyncState(account = account, accountState = Authenticated)

        val updated = provider.itemsFlow.first { it.single().title.text == Text.String(DISPLAY_NAME) }.single()

        assertEquals(Text.String(DISPLAY_NAME), updated.title.text)
    }

    @OptIn(ExperimentalCoroutinesApi::class)
    @Test
    fun `GIVEN the avatar was downloaded WHEN another account detail changes THEN don't download the avatar again`() =
        runTest {
            syncState.value =
                SyncState(
                    account = account.copy(avatar = Avatar(url = AVATAR_URL, isDefault = false)),
                    accountState = Authenticated,
                )
            val httpClient = clientReturning(avatarBytes)
            val provider = provider(httpClient = httpClient)
            provider.itemsFlow.first { (it.single() as StandardMenuItem).icon is MenuItemIconBitmap }

            syncState.value = syncState.value.copy(status = SyncStatus.Started)
            runCurrent()

            verify(exactly = 1) { httpClient.fetch(any()) }
        }

    @Test
    fun `GIVEN someone is signed in WHEN clicking the item THEN show their account settings in place of the menu`() =
        runTest {
            val menu = FakeMenuHost()

            provider().onEvent(MenuAction.Navigate.MozillaAccount(Authenticated, MenuAccessPoint.Browser), menu)

            assertEquals(NavGraphDirections.actionGlobalAccountSettingsFragment(), menu.directions)
        }

    @Test
    fun `GIVEN the account needs signing in again WHEN clicking the item THEN offer signing back in`() = runTest {
        val menu = FakeMenuHost()

        provider().onEvent(MenuAction.Navigate.MozillaAccount(AuthenticationProblem, MenuAccessPoint.Browser), menu)

        assertEquals(
            NavGraphDirections.actionGlobalAccountProblemFragment(entrypoint = FenixFxAEntryPoint.BrowserToolbar),
            menu.directions,
        )
    }

    @Test
    fun `GIVEN nobody is signed in WHEN clicking the item THEN offer signing in`() = runTest {
        val menu = FakeMenuHost()

        provider().onEvent(MenuAction.Navigate.MozillaAccount(NotAuthenticated, MenuAccessPoint.Browser), menu)

        assertEquals(
            NavGraphDirections.actionGlobalTurnOnSync(entrypoint = FenixFxAEntryPoint.BrowserToolbar),
            menu.directions,
        )
    }

    @Test
    fun `WHEN asked about the events of the account item THEN handle only those`() = runTest {
        val provider = provider()

        assertTrue(provider.handles(MenuAction.Navigate.MozillaAccount(Authenticated, MenuAccessPoint.Browser)))
        assertFalse(provider.handles(MenuAction.Navigate.Settings))
    }

    // The account is observed and its avatar downloaded on a scope that runTest cancels at the end of each test.
    private fun TestScope.provider(
        httpClient: Client = mockk(relaxed = true),
        target: MenuTarget = MenuTarget.BrowserTab,
    ) =
        MozillaAccountMenuItemProvider(
            syncStore = syncStore,
            httpClient = httpClient,
            context = testContext,
            accessPoint = MenuAccessPoint.Browser,
            target = target,
            scope = backgroundScope,
        )

    private fun clientReturning(bytes: ByteArray): Client = mockk {
        every { fetch(any()) } answers
            {
                Response(
                    url = AVATAR_URL,
                    status = 200,
                    headers = MutableHeaders(),
                    body = Response.Body(ByteArrayInputStream(bytes)),
                )
            }
    }

    private fun expectedItem(
        title: Text,
        summary: Text,
        accountState: AccountState,
    ) =
        StandardMenuItem(
            title = MenuItemTitle(title),
            summary = MenuItemSummary(text = summary, maxLines = SUMMARY_MAX_LINES),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_avatar_circle_24),
            onClickEvent =
                MenuAction.Navigate.MozillaAccount(
                    accountState = accountState,
                    accesspoint = MenuAccessPoint.Browser,
                ),
        )

    private val avatarBytes: ByteArray
        get() = javaClass.getResourceAsStream("/png/avatar.png")!!.buffered().readBytes()

    private companion object {
        const val DISPLAY_NAME = "Mozilla user"
        const val EMAIL = "user@mozilla.org"
        const val AVATAR_URL = "https://mozilla.org/avatar.png"
        const val SUMMARY_MAX_LINES = 2

        val account =
            Account(
                uid = "uid",
                email = EMAIL,
                avatar = null,
                displayName = DISPLAY_NAME,
            )
    }
}
